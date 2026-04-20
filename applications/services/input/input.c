#include "input.h"

#include "input_settings.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <furi.h>
#include <furi_hal_gpio.h>
#define USE_MCP23017
#ifdef USE_MCP23017
#include <furi_hal_mcp23017.h>
#endif
#include <furi_hal_vibro.h>
#include <toolbox/cli/cli_command.h>
#include <cli/cli_main_commands.h>
#include <toolbox/pipe.h>

#define INPUT_DEBOUNCE_TICKS_HALF (INPUT_DEBOUNCE_TICKS / 2)
#define INPUT_PRESS_TICKS         200
#define INPUT_LONG_PRESS_COUNTS   5
#define INPUT_THREAD_FLAG_ISR     0x00000001

#define TAG "Input"

// Temporary testing helper: define to bypass debounce and publish immediately
// when a raw MCP state change is detected. Disable for normal builds.
// #define INPUT_MCP_IMMEDIATE_PUBLISH
#define MCP_ADDR 0x20

/** Input pin state */
typedef struct {
    const InputPin* pin;
    // State
    volatile bool state;
    volatile uint8_t debounce;
    FuriTimer* press_timer;
    FuriPubSub* event_pubsub;
    volatile uint8_t press_counter;
    volatile uint32_t counter;
} InputPinState;

// #define INPUT_DEBUG

// When using MCP23017 we will read cached state from the expander.
static volatile uint16_t g_mcp_gpio_state = 0;
#define MCP_DEFAULT(pin) ((uint16_t)(1u << (pin)))

// Default mapping: map input indices to MCP pins 0..N-1. Update this array
// to match your hardware wiring where MCP GPA0..GPA7 and GPB0..GPB7
// correspond to external connectors A0..A7, B0..B7, etc.
// Wiring mapping provided by user: MCP physical pins 0..5 correspond to keys:
// pin0 -> key 3 (Back)
// pin1 -> key 4 (OK)
// pin2 -> key 1 (Down)
// pin3 -> key 0 (Up)
// pin4 -> key 5 (Left)
// pin5 -> key 2 (Right)
// We invert this to get per-input-index -> MCP pin mapping:
// index 0 (Up)  -> MCP pin 3
// index 1 (Down)-> MCP pin 2
// index 2 (Right)-> MCP pin 5
// index 3 (Left)-> MCP pin 0
// index 4 (OK)  -> MCP pin 1
// index 5 (Back)-> MCP pin 4
static const uint8_t mcp_pin_map_default[] = {
    0, // input_pins[0] (Up)    -> MCP pin 0 (A0)
    4, // input_pins[1] (Down)  -> MCP pin 4 (A4)
    1, // input_pins[2] (Right) -> MCP pin 1 (A1)
    5, // input_pins[3] (Left)  -> MCP pin 5 (A5)
    2, // input_pins[4] (OK)    -> MCP pin 2 (A2 / enter)
    3, // input_pins[5] (Back)  -> MCP pin 3 (A3)
};

static uint16_t input_mcp_mask_for_index(size_t idx) {
    size_t cnt = sizeof(mcp_pin_map_default) / sizeof(mcp_pin_map_default[0]);
    if(idx < cnt) {
        uint8_t bit = mcp_pin_map_default[idx];
        return (uint16_t)(1u << bit);
    }
    // fallback: no mapping
    return 0;
}
#define GPIO_Read_MCP_BY_IDX(idx) (((g_mcp_gpio_state & input_mcp_mask_for_index(idx)) != 0) ^ (input_pins[idx].inverted))

void input_press_timer_callback(void* arg) {
    if(!arg) return;
    InputPinState* input_pin = arg;
    if(!input_pin->pin) return;

    InputEvent event;
    event.sequence_source = INPUT_SEQUENCE_SOURCE_HARDWARE;
    event.sequence_counter = input_pin->counter;
    event.key = input_pin->pin->key;
    input_pin->press_counter++;
    if(input_pin->press_counter == INPUT_LONG_PRESS_COUNTS) {
        event.type = InputTypeLong;
        if(input_pin->event_pubsub) {
            FURI_LOG_I(TAG, "Timer publish: key=%d type=Long seq=%u", event.key, event.sequence_counter);
            furi_pubsub_publish(input_pin->event_pubsub, &event);
        }
    } else if(input_pin->press_counter > INPUT_LONG_PRESS_COUNTS) {
        input_pin->press_counter--;
        event.type = InputTypeRepeat;
        if(input_pin->event_pubsub) {
            FURI_LOG_I(TAG, "Timer publish: key=%d type=Repeat seq=%u", event.key, event.sequence_counter);
            furi_pubsub_publish(input_pin->event_pubsub, &event);
        }
    }
}

void input_isr(void* _ctx) {
    FuriThreadId thread_id = (FuriThreadId)_ctx;
    furi_thread_flags_set(thread_id, INPUT_THREAD_FLAG_ISR);
}

const char* input_get_key_name(InputKey key) {
    for(size_t i = 0; i < input_pins_count; i++) {
        if(input_pins[i].key == key) {
            return input_pins[i].name;
        }
    }
    return "Unknown";
}

const char* input_get_type_name(InputType type) {
    switch(type) {
    case InputTypePress:
        return "Press";
    case InputTypeRelease:
        return "Release";
    case InputTypeShort:
        return "Short";
    case InputTypeLong:
        return "Long";
    case InputTypeRepeat:
        return "Repeat";
    default:
        return "Unknown";
    }
}

int32_t input_srv(void* p) {
    UNUSED(p);

    const FuriThreadId thread_id = furi_thread_get_current_id();
    FuriPubSub* event_pubsub = furi_pubsub_alloc();
    FuriPubSub* ascii_pubsub = furi_pubsub_alloc();
    uint32_t counter = 1;
    furi_record_create(RECORD_INPUT_EVENTS, event_pubsub);
    furi_record_create(RECORD_ASCII_EVENTS, ascii_pubsub);

    //define object input_settings, take memory load (or init) settings and create record for access to settings structure from outside
    InputSettings* settings = malloc(sizeof(InputSettings));
    input_settings_load(settings);
    FURI_LOG_I(TAG, "furi_record_create INPUT_SETTINGS");
    furi_record_create(RECORD_INPUT_SETTINGS, settings);
    FURI_LOG_I(TAG, "furi_record_create INPUT_SETTINGS done");

#ifdef INPUT_DEBUG
    furi_hal_gpio_init_simple(&gpio_ext_pa4, GpioModeOutputPushPull);
#endif

    InputPinState* pin_states = malloc(sizeof(InputPinState) * input_pins_count);
    FURI_LOG_I(TAG, "Allocated pin states");
    if(!pin_states) {
        FURI_LOG_E(TAG, "Failed to allocate pin states");
        return 0;
    }
    FURI_LOG_I(TAG, "Initializing input pins");
    for(size_t i = 0; i < input_pins_count; i++) {
        // Using MCP23017 only: register a single callback later, don't attach per-pin
        (void)input_pins;
        FURI_LOG_I(TAG, "Using MCP23017, skipping GPIO callback attach for pin %u", (unsigned)i);
        pin_states[i].pin = &input_pins[i];
        pin_states[i].state = GPIO_Read_MCP_BY_IDX(i);
        pin_states[i].debounce = INPUT_DEBOUNCE_TICKS_HALF;
        pin_states[i].press_timer = furi_timer_alloc(
            input_press_timer_callback, FuriTimerTypePeriodic, &pin_states[i]);
        if(!pin_states[i].press_timer) {
            FURI_LOG_W(TAG, "Timer alloc failed for pin %u", (unsigned)i);
        }
        pin_states[i].event_pubsub = event_pubsub;
        pin_states[i].press_counter = 0;
        FURI_LOG_I(TAG, "Initialized pin %s", input_pins[i].name);
    }

    // Initialize MCP23017 at default address (wrapper)
    if(!furi_hal_mcp23017_init()) {
        FURI_LOG_E(TAG, "MCP23017 init failed");
    } else {
        FURI_LOG_I(TAG, "MCP23017 initialized");
        // Build mask of pins we want as inputs based on MCP mapping.
        // Use input_mcp_mask_for_index to map input indices -> MCP bit mask.
        uint16_t mask = 0;
        for(size_t i = 0; i < input_pins_count; i++) {
            mask |= input_mcp_mask_for_index(i);
        }
        // Pass full 16-bit mask: lower 8 bits -> GPIOA, upper 8 bits -> GPIOB
        furi_hal_mcp23017_configure_interrupts(mask);

        // Read current MCP state and seed cache so we don't generate spurious
        // events from the initial snapshot. Also mark debounces as stable.
        uint16_t tmp_state = 0;
        if(furi_hal_mcp23017_read_gpio(&tmp_state)) {
            g_mcp_gpio_state = tmp_state;
            for(size_t j = 0; j < input_pins_count; j++) {
                // Initialize pin state according to MCP reading
                pin_states[j].state = GPIO_Read_MCP_BY_IDX(j);
                pin_states[j].debounce = INPUT_DEBOUNCE_TICKS; // stable
            }
        }

        // Attach INT callback: when MCP reports interrupt, call input_isr
        furi_hal_mcp23017_attach_int(input_isr, (void*)thread_id);
        // Ensure the MCU INT pin is configured as an interrupt input with pull-up
        furi_hal_gpio_init(&gpio_mcp_int, GpioModeInterruptRiseFall, GpioPullUp, GpioSpeedLow);
        // Attach the expander INT line to the MCU EXTI pin
        furi_hal_gpio_add_int_callback(&gpio_mcp_int, (GpioExtiCallback)furi_hal_mcp23017_handle_int, NULL);
        furi_hal_gpio_enable_int_callback(&gpio_mcp_int);
    }

    while(1) {
        bool is_changing = false;
        for(size_t i = 0; i < input_pins_count; i++) {
            // Update MCP state cache first if available
#ifdef USE_MCP23017
            uint16_t new_state = 0;
            if(furi_hal_mcp23017_read_gpio(&new_state)) {
                uint16_t prev = g_mcp_gpio_state;
                g_mcp_gpio_state = new_state;
                if(prev != new_state) {
                    // FURI_LOG_I(TAG, "MCP GPIO state changed 0x%04X -> 0x%04X", prev, new_state);
                    uint16_t changed = prev ^ new_state;
                    for(size_t j = 0; j < input_pins_count; j++) {
                        uint16_t mask = input_mcp_mask_for_index(j);
                        if(mask && (changed & mask)) {
                            bool now = (new_state & mask) != 0;
                            UNUSED(now);
                            // FURI_LOG_I(
                            //     TAG,
                            //     "  Input idx %u (%s) MCPbit %u changed: %s",
                            //     (unsigned)j,
                            //     input_pins[j].name,
                            //     (unsigned)(mcp_pin_map_default[j]),
                            //     now ? "1" : "0");
                        }
                    }
                }
            }
#endif
            bool state;
#ifdef USE_MCP23017
            state = GPIO_Read_MCP_BY_IDX(i);
#else
            state = GPIO_Read(pin_states[i]);
#endif
            if(state) {
                if(pin_states[i].debounce < INPUT_DEBOUNCE_TICKS) pin_states[i].debounce += 1;
            } else {
                if(pin_states[i].debounce > 0) pin_states[i].debounce -= 1;
            }

            if(pin_states[i].debounce > 0 && pin_states[i].debounce < INPUT_DEBOUNCE_TICKS) {
                is_changing = true;
            } else if(pin_states[i].state != state) {
                pin_states[i].state = state;

                // Common state info
                InputEvent event;
                event.sequence_source = INPUT_SEQUENCE_SOURCE_HARDWARE;
                event.key = pin_states[i].pin->key;

                // Short / Long / Repeat timer routine
                if(state) {
                    pin_states[i].counter = counter++;
                    event.sequence_counter = pin_states[i].counter;
                    furi_timer_start(pin_states[i].press_timer, INPUT_PRESS_TICKS);
                } else {
                    event.sequence_counter = pin_states[i].counter;
                    if(pin_states[i].press_timer) {
                        furi_timer_stop(pin_states[i].press_timer);
                        while(furi_timer_is_running(pin_states[i].press_timer)) furi_delay_tick(1);
                    }
                    if(pin_states[i].press_counter < INPUT_LONG_PRESS_COUNTS) {
                        event.type = InputTypeShort;
                        // FURI_LOG_I(TAG, "Publish Short: key=%d seq=%u", event.key, event.sequence_counter);
                        furi_pubsub_publish(event_pubsub, &event);
                    }
                    pin_states[i].press_counter = 0;
                }

                // Send Press/Release event
                event.type = pin_states[i].state ? InputTypePress : InputTypeRelease;
                // FURI_LOG_I(TAG, "Publish %s: key=%d seq=%u", pin_states[i].state ? "Press" : "Release", event.key, event.sequence_counter);
                furi_pubsub_publish(event_pubsub, &event);

#ifdef INPUT_MCP_IMMEDIATE_PUBLISH
                // Also publish an immediate short event for quicker UI testing
                InputEvent dbg = event;
                dbg.type = InputTypeShort;
                // FURI_LOG_I(TAG, "Immediate publish debug: key=%d", dbg.key);
                furi_pubsub_publish(event_pubsub, &dbg);
#endif
                // vibro signal if user setup vibro touch level in Settings-Input.
                if(settings->vibro_touch_level &&
                   ((1 << event.type) & settings->vibro_touch_trigger_mask)) {
                    //delay 1 ticks for compatibility with rgb_backlight_mod
                    furi_delay_tick(1);
                    furi_hal_vibro_on(true);
                    furi_delay_tick(settings->vibro_touch_level * 10);
                    furi_hal_vibro_on(false);
                }
            }
        }

        if(is_changing) {
#ifdef INPUT_DEBUG
            // furi_hal_gpio_write(&gpio_ext_pa4, 1);
#endif
            furi_delay_tick(1);
        } else {
#ifdef INPUT_DEBUG
            // furi_hal_gpio_write(&gpio_ext_pa4, 0);
#endif
            furi_thread_flags_wait(INPUT_THREAD_FLAG_ISR, FuriFlagWaitAny, FuriWaitForever);
        }
    }

    return 0;
}
