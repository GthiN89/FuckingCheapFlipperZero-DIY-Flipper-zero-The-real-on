#include "input.h"
#include "input_settings.h"

#include <stdbool.h>
#include <stdint.h>
#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_vibro.h>
#include <furi_hal_resources.h>
#include <furi_hal.h>
#include <furi_hal_mcp23017.h>

#define TAG "InputSrv"

#define INPUT_PRESS_TICKS 500
#define INPUT_LONG_PRESS_COUNTS 5
#define INPUT_THREAD_FLAG_ISR 0x00000001
#define INPUT_DEBOUNCE_TICKS 4
#define INPUT_DEBOUNCE_TICKS_HALF (INPUT_DEBOUNCE_TICKS / 2)

/**
 * MCP23017 Mapping based on Schematic:
 * GPA7 - UP (7)
 * GPA3 - DOWN (3) <--- Pin 24
 * GPA5 - RIGHT (5)
 * GPA4 - LEFT (4)
 * GPA6 - OK (6)
 * GPA2 - BACK (2)
 */
static const uint8_t mcp_map[InputKeyMAX] = {
    [InputKeyUp]    = 7,
    [InputKeyDown]  = 3,
    [InputKeyRight] = 5,
    [InputKeyLeft]  = 4,
    [InputKeyOk]    = 6,
    [InputKeyBack]  = 2,
};

static volatile uint16_t g_mcp_gpio_state = 0;

typedef struct {
    const InputPin* pin;
    volatile bool state;
    volatile uint8_t debounce;
    FuriTimer* press_timer;
    FuriPubSub* event_pubsub;
    volatile uint8_t press_counter;
    volatile uint32_t counter;
} InputPinState;

/* --- Linker mandated functions --- */

const char* input_get_key_name(InputKey key) {
    switch(key) {
    case InputKeyOk: return "Ok";
    case InputKeyBack: return "Back";
    case InputKeyUp: return "Up";
    case InputKeyDown: return "Down";
    case InputKeyRight: return "Right";
    case InputKeyLeft: return "Left";
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

/* --- Internal Helpers --- */

static uint16_t input_mcp_mask_for_key(InputKey key) {
    if(key < InputKeyMAX) {
        return (uint16_t)(1u << mcp_map[key]);
    }
    return 0;
}

void input_press_timer_callback(void* arg) {
    if(!arg) return;
    InputPinState* input_pin = arg;
    InputEvent event;
    event.sequence_source = INPUT_SEQUENCE_SOURCE_HARDWARE;
    event.sequence_counter = input_pin->counter;
    event.key = input_pin->pin->key;
    input_pin->press_counter++;

    if(input_pin->press_counter == INPUT_LONG_PRESS_COUNTS) {
        event.type = InputTypeLong;
        furi_pubsub_publish(input_pin->event_pubsub, &event);
    } else if(input_pin->press_counter > INPUT_LONG_PRESS_COUNTS) {
        event.type = InputTypeRepeat;
        furi_pubsub_publish(input_pin->event_pubsub, &event);
    }
}

void input_isr(void* _ctx) {
    FuriThreadId thread_id = (FuriThreadId)_ctx;
    furi_thread_flags_set(thread_id, INPUT_THREAD_FLAG_ISR);
}

/* --- Main Service --- */

int32_t input_srv(void* p) {
    UNUSED(p);

    FURI_LOG_I(TAG, "Starting Input Service with Debugging");

    const FuriThreadId thread_id = furi_thread_get_current_id();
    FuriPubSub* event_pubsub = furi_pubsub_alloc();
    FuriPubSub* ascii_pubsub = furi_pubsub_alloc();
    uint32_t counter = 1;

    furi_record_create(RECORD_INPUT_EVENTS, event_pubsub);
    furi_record_create(RECORD_ASCII_EVENTS, ascii_pubsub);

    InputSettings* settings = malloc(sizeof(InputSettings));
    input_settings_load(settings);
    furi_record_create(RECORD_INPUT_SETTINGS, settings);

    InputPinState* pin_states = malloc(sizeof(InputPinState) * input_pins_count);
    uint16_t mcp_interrupt_mask = 0;

    for(size_t i = 0; i < input_pins_count; i++) {
        pin_states[i].pin = &input_pins[i];
        pin_states[i].state = false;
        pin_states[i].debounce = INPUT_DEBOUNCE_TICKS_HALF;
        pin_states[i].press_timer = furi_timer_alloc(
            input_press_timer_callback, FuriTimerTypePeriodic, &pin_states[i]);
        pin_states[i].event_pubsub = event_pubsub;
        pin_states[i].press_counter = 0;
        pin_states[i].counter = 0;

        uint16_t key_mask = input_mcp_mask_for_key(pin_states[i].pin->key);
        mcp_interrupt_mask |= key_mask;
        
        if(pin_states[i].pin->key == InputKeyDown) {
            FURI_LOG_I(TAG, "Init: Down Button detected, Mask Bit: %d", mcp_map[InputKeyDown]);
        }
    }

    FURI_LOG_I(TAG, "Calculated MCP Interrupt Mask: 0x%04X", mcp_interrupt_mask);

    if(furi_hal_mcp23017_init()) {
        FURI_LOG_I(TAG, "MCP23017 Hardware Init OK");
        furi_hal_mcp23017_led_init();
        furi_hal_mcp23017_configure_interrupts(mcp_interrupt_mask);

        // Hardware IRQ Pin: Pin B1 (Pin 22 on STM32)
        furi_hal_mcp23017_attach_int(input_isr, (void*)thread_id);
        furi_hal_gpio_init(&gpio_mcp_int, GpioModeInterruptRiseFall, GpioPullUp, GpioSpeedLow);
        furi_hal_gpio_add_int_callback(&gpio_mcp_int, (GpioExtiCallback)furi_hal_mcp23017_handle_int, NULL);
        furi_hal_gpio_enable_int_callback(&gpio_mcp_int);

        uint16_t tmp = 0;
        if(furi_hal_mcp23017_read_gpio(&tmp)) {
            g_mcp_gpio_state = tmp;
            FURI_LOG_I(TAG, "Initial MCP Register Read: 0x%04X", tmp);
        }
    } else {
        FURI_LOG_E(TAG, "MCP23017 Hardware Init FAILED");
    }

    while(1) {
        bool is_changing = false;
        uint16_t current_bits = 0;
        
        if(furi_hal_mcp23017_read_gpio(&current_bits)) {
            if(current_bits != g_mcp_gpio_state) {
                FURI_LOG_I(TAG, "I2C State Change: 0x%04X -> 0x%04X", g_mcp_gpio_state, current_bits);
            }
            g_mcp_gpio_state = current_bits;
        }

        for(size_t i = 0; i < input_pins_count; i++) {
            InputPinState* ps = &pin_states[i];
            InputKey key = ps->pin->key;

            if(key >= InputKeyMAX) continue;

            uint8_t bit = mcp_map[key];
            bool raw_bit_high = (g_mcp_gpio_state & (1u << bit)) != 0;
            bool logical_state = raw_bit_high ^ ps->pin->inverted;

            // Specific debug for Down Button logic
            if(key == InputKeyDown && (ps->state != logical_state || ps->debounce > 0)) {
                FURI_LOG_I(TAG, "DOWN Key Logic: Bit3=%d Inv=%d Logical=%d Debounce=%d", 
                          raw_bit_high, ps->pin->inverted, logical_state, ps->debounce);
            }

            if(logical_state) {
                if(ps->debounce < INPUT_DEBOUNCE_TICKS) ps->debounce++;
            } else {
                if(ps->debounce > 0) ps->debounce--;
            }

            if(ps->debounce > 0 && ps->debounce < INPUT_DEBOUNCE_TICKS) {
                is_changing = true;
            } else if(ps->state != logical_state) {
                ps->state = logical_state;
                InputEvent ev = {.sequence_source = INPUT_SEQUENCE_SOURCE_HARDWARE, .key = key};

                if(logical_state) {
                    ps->counter = counter++;
                    ev.sequence_counter = ps->counter;
                    ev.type = InputTypePress;
                    furi_timer_start(ps->press_timer, INPUT_PRESS_TICKS);
                    
                    FURI_LOG_I(TAG, "EVENT: Key %s Press", input_get_key_name(key));
                    furi_pubsub_publish(event_pubsub, &ev);
                } else {
                    ev.sequence_counter = ps->counter;
                    if(ps->press_timer) furi_timer_stop(ps->press_timer);
                    if(ps->press_counter < INPUT_LONG_PRESS_COUNTS) {
                        ev.type = InputTypeShort;
                        furi_pubsub_publish(event_pubsub, &ev);
                    }
                    ps->press_counter = 0;
                    ev.type = InputTypeRelease;
                    FURI_LOG_I(TAG, "EVENT: Key %s Release", input_get_key_name(key));
                    furi_pubsub_publish(event_pubsub, &ev);
                }

                if(settings->vibro_touch_level && logical_state) {
                    furi_hal_vibro_on(true);
                    furi_delay_ms(settings->vibro_touch_level * 10);
                    furi_hal_vibro_on(false);
                }
            }
        }

        if(is_changing) {
            furi_delay_tick(1);
        } else {
            furi_thread_flags_wait(INPUT_THREAD_FLAG_ISR, FuriFlagWaitAny, FuriWaitForever);
            furi_thread_flags_clear(INPUT_THREAD_FLAG_ISR);
        }
    }
    return 0;
}