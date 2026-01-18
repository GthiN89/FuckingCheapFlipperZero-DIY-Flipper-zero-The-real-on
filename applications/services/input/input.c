#include "input.h"
#include "input_settings.h"

#include <stdbool.h>
#include <stdint.h>
#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_spi.h>
#include <furi_hal_vibro.h>
#include <furi_hal_resources.h>
#include <furi_hal.h>

#define INPUT_PRESS_TICKS 500
#define INPUT_MAX_PRESS_DURATION_TICKS 5000 // 5 seconds
#define INPUT_LONG_PRESS_COUNTS 2
#define INPUT_THREAD_FLAG_ISR 0x00000001
#define TAG "InputSrv"

// Debounce configuration from your working version
#define DEBOUNCE_CONSECUTIVE_READS 2
#define DEBOUNCE_MAX_TRIES 10
#define DEBOUNCE_READ_DELAY_MS 2

// Helper macro for binary logging
#define BYTE_TO_BIN_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BIN(byte) \
  ((byte) & 0x80 ? '1' : '0'), \
  ((byte) & 0x40 ? '1' : '0'), \
  ((byte) & 0x20 ? '1' : '0'), \
  ((byte) & 0x10 ? '1' : '0'), \
  ((byte) & 0x08 ? '1' : '0'), \
  ((byte) & 0x04 ? '1' : '0'), \
  ((byte) & 0x02 ? '1' : '0'), \
  ((byte) & 0x01 ? '1' : '0')

typedef struct {
    const InputPin* pin;
    volatile bool state;
    FuriTimer* press_timer;
    FuriPubSub* event_pubsub;
    volatile uint8_t press_counter;
    volatile uint32_t counter;
} InputPinState;

static InputKey decode_key_from_byte(uint8_t byte) {
    switch(byte) {
    case 0b00100000: return InputKeyRight;  
    case 0b00010000: return InputKeyOk;          
    case 0b00000010: return InputKeyLeft;        
    case 0b00001000: return InputKeyUp;         
    case 0b10000000: return InputKeyDown;     
    case 0b01000000: return InputKeyBack;    
    
    default: return InputKeyMAX;
    }
}

// Helper function to encapsulate the shift register read sequence
static uint8_t read_shift_register(const FuriHalSpiBusHandle* spi_handle) {
    uint8_t value = 0;

    // Latch and read shift register
    furi_hal_gpio_write(&gpio_button_sr_latch, false);
    furi_delay_us(5);
    furi_hal_gpio_write(&gpio_button_sr_latch, true);

    furi_hal_spi_acquire((FuriHalSpiBusHandle*)spi_handle);
    furi_hal_spi_bus_rx((FuriHalSpiBusHandle*)spi_handle, &value, 1, 100);
    furi_hal_spi_release((FuriHalSpiBusHandle*)spi_handle);
        FURI_LOG_D(TAG, "SR Read -> " BYTE_TO_BIN_PATTERN, BYTE_TO_BIN(~value));
        
    return ~value; // Invert because buttons are active-low
}

void input_press_timer_callback(void* arg) {
    InputPinState* input_pin = arg;
    InputEvent event = {
        .sequence_source = INPUT_SEQUENCE_SOURCE_HARDWARE,
        .sequence_counter = input_pin->counter,
        .key = input_pin->pin->key};

    input_pin->press_counter++;
    if(input_pin->press_counter == INPUT_LONG_PRESS_COUNTS) {
        event.type = InputTypeLong;
        FURI_LOG_D(TAG, "Timer -> KeyID: %d, Type: Long", event.key);
        furi_pubsub_publish(input_pin->event_pubsub, &event);
    } else if(input_pin->press_counter > INPUT_LONG_PRESS_COUNTS) {
        event.type = InputTypeRepeat;
        FURI_LOG_D(TAG, "Timer -> KeyID: %d, Type: Repeat", event.key);
        furi_pubsub_publish(input_pin->event_pubsub, &event);
    }
}

void input_isr(void* _ctx) {
    FuriThreadId thread_id = (FuriThreadId)_ctx;
    furi_thread_flags_set(thread_id, INPUT_THREAD_FLAG_ISR);
}

const char* input_get_key_name(InputKey key) {
    switch(key) {
    case InputKeyOk: return "InputKeyOk";
    case InputKeyBack: return "InputKeyBack";
    case InputKeyUp: return "InputKeyUp";
    case InputKeyDown: return "InputKeyDown";
    case InputKeyRight: return "InputKeyRight";
    case InputKeyLeft: return "InputKeyLeft";
    default: return "Unknown";
    }
}

const char* input_get_type_name(InputType type) {
    switch(type) {
    case InputTypePress: return "Press";
    case InputTypeRelease: return "Release";
    case InputTypeShort: return "Short";
    case InputTypeLong: return "Long";
    case InputTypeRepeat: return "Repeat";
    default: return "Unknown";
    }
}

int32_t input_srv(void* p) {
    UNUSED(p);
    const FuriThreadId thread_id = furi_thread_get_current_id();
    FuriPubSub* event_pubsub = furi_pubsub_alloc();
    furi_record_create(RECORD_INPUT_EVENTS, event_pubsub);
    FuriPubSub* ascii_pubsub = furi_pubsub_alloc();
    furi_record_create(RECORD_ASCII_EVENTS, ascii_pubsub);
    InputSettings* settings = malloc(sizeof(InputSettings));
    input_settings_load(settings);
    furi_record_create(RECORD_INPUT_SETTINGS, settings);

    InputPinState pin_states[InputKeyMAX] = {0};
    for(size_t i = 0; i < InputKeyMAX; i++) {
        pin_states[i].pin = NULL;
        for(size_t j = 0; j < input_pins_count; j++) {
            if(input_pins[j].key == (InputKey)i) {
                pin_states[i].pin = &input_pins[j];
                break;
            }
        }
        pin_states[i].press_timer =
            furi_timer_alloc(input_press_timer_callback, FuriTimerTypePeriodic, &pin_states[i]);

        pin_states[i].event_pubsub = event_pubsub;
        pin_states[i].press_counter = 0;
        pin_states[i].state = false;
    }

    furi_hal_gpio_init(&gpio_button_sr_latch, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(&gpio_button_sr_latch, true);
    const FuriHalSpiBusHandle* spi_handle = &furi_hal_spi_bus_handle_button_sr;

    furi_delay_ms(50);
    uint8_t buttons_released_state = read_shift_register(spi_handle);
    FURI_LOG_I(
        TAG,
        "Initial button state (released): " BYTE_TO_BIN_PATTERN,
        BYTE_TO_BIN(buttons_released_state));

    FURI_LOG_I(TAG, "Input Service Starting in Interrupt Mode");
    furi_hal_gpio_init(&gpio_button_IRQ, GpioModeInterruptRiseFall, GpioPullUp, GpioSpeedLow);
    furi_hal_gpio_add_int_callback(&gpio_button_IRQ, input_isr, (void*)thread_id);

    int counter = 0;
    static uint8_t last_debounced_state = 0;
    last_debounced_state = buttons_released_state;
    static uint32_t press_start_ticks[InputKeyMAX] = {0};

    while(1) {
        uint32_t flags = furi_thread_flags_wait(INPUT_THREAD_FLAG_ISR, FuriFlagWaitAny, 100);

        if(flags & INPUT_THREAD_FLAG_ISR) {
            uint8_t current_byte = 0;
            uint8_t last_byte = 0;
            int consecutive = 0;
            int tries = 0;

            do {
                FURI_LOG_D(TAG, "IRQ Trigger");
                current_byte = read_shift_register(spi_handle);
                if(tries > 0) {
                    if(current_byte == last_byte) {
                        consecutive++;
                    } else {
                        consecutive = 0;
                    }
                }
                tries++;
                if(consecutive < DEBOUNCE_CONSECUTIVE_READS) {
                    furi_delay_ms(DEBOUNCE_READ_DELAY_MS);
                }
            } while(consecutive < DEBOUNCE_CONSECUTIVE_READS && tries < DEBOUNCE_MAX_TRIES);

            if(current_byte == last_debounced_state) {
                continue; // Spurious interrupt, ignore.
            }

            FURI_LOG_D(TAG, "State change: " BYTE_TO_BIN_PATTERN, BYTE_TO_BIN(current_byte));

            InputKey previous_key = decode_key_from_byte(last_debounced_state);
            InputKey current_key = decode_key_from_byte(current_byte);

            if(previous_key != InputKeyMAX && (current_key != previous_key)) {
                uint32_t press_duration_ticks = furi_get_tick() - press_start_ticks[previous_key];
                InputEvent event = {
                    .key = previous_key, .type = InputTypeShort, .sequence_counter = counter++};

                if(press_duration_ticks >= INPUT_PRESS_TICKS) {
                    event.type = InputTypeLong;
                    FURI_LOG_D(
                        TAG, "Action -> Key: %s, Type: Long", input_get_key_name(event.key));
                } else {
                    FURI_LOG_D(
                        TAG, "Action -> Key: %s, Type: Short", input_get_key_name(event.key));
                }
                furi_pubsub_publish(event_pubsub, &event);

                event.type = InputTypeRelease;
                FURI_LOG_D(
                    TAG, "Action -> Key: %s, Type: Release", input_get_key_name(event.key));
                furi_pubsub_publish(event_pubsub, &event);
            }

            if(current_key != InputKeyMAX && (current_key != previous_key)) {
                press_start_ticks[current_key] = furi_get_tick();
                InputEvent event = {
                    .key = current_key, .type = InputTypePress, .sequence_counter = counter++};
                furi_pubsub_publish(event_pubsub, &event);
                FURI_LOG_D(TAG, "Key Press Detected: %s", input_get_key_name(current_key));
            }

            last_debounced_state = current_byte;
        } else if(flags & FuriFlagErrorTimeout) {
            if(last_debounced_state != buttons_released_state) {
                InputKey key_being_pressed = decode_key_from_byte(last_debounced_state);
                if(key_being_pressed != InputKeyMAX) {
                    uint32_t press_duration_ticks =
                        furi_get_tick() - press_start_ticks[key_being_pressed];

                    if(press_duration_ticks >= INPUT_MAX_PRESS_DURATION_TICKS) {
                        FURI_LOG_W(
                            TAG,
                            "Max press duration for %s exceeded. Forcing release.",
                            input_get_key_name(key_being_pressed));

                        InputEvent event = {
                            .key = key_being_pressed,
                            .type = InputTypeLong,
                            .sequence_counter = counter++};
                        furi_pubsub_publish(event_pubsub, &event);

                        event.type = InputTypeRelease;
                        furi_pubsub_publish(event_pubsub, &event);
                        FURI_LOG_D(
                            TAG,
                            "Action -> Key: %s, Type: Release (Forced)",
                            input_get_key_name(event.key));

                        last_debounced_state = buttons_released_state;
                    }
                }
            }
        }
    }

    // Cleanup
    furi_hal_gpio_remove_int_callback(&gpio_button_IRQ);
    for(size_t i = 0; i < InputKeyMAX; i++) {
        if(pin_states[i].press_timer != NULL) {
            furi_timer_stop(pin_states[i].press_timer);
            furi_timer_free(pin_states[i].press_timer);
        }
    }
    furi_pubsub_free(event_pubsub);
    furi_pubsub_free(ascii_pubsub);
    free(settings);

    return 0;
}