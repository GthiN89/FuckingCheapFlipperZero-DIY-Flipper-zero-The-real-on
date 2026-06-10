#include <core/common_defines.h>
#include <furi_hal_resources.h>
#include <furi_hal_light.h>
#include <furi_hal_mcp23017.h>
#include <stdint.h>
#include <momentum/momentum.h>
#include <rgb_backlight.h>

#define LED_CURRENT_RED   (50u)
#define LED_CURRENT_GREEN (50u)
#define LED_CURRENT_BLUE  (50u)
#define LED_CURRENT_WHITE (150u)

void furi_hal_light_init(void) {
    // Initialize RGB LED on MCP23017
    furi_hal_mcp23017_led_init();
}

void furi_hal_light_set(Light light, uint8_t value) {
    // MCP23017 only supports on/off, not PWM brightness
    // Treat any value > 0x7F as "on"
    bool on = (value > 0x7F);
    
    if(light & LightRed) {
        furi_hal_mcp23017_led_set_red(on);
    }
    if(light & LightGreen) {
        furi_hal_mcp23017_led_set_green(on);
    }
    if(light & LightBlue) {
        furi_hal_mcp23017_led_set_blue(on);
    }
    if(light & LightBacklight) {
        if(momentum_settings.rgb_backlight) {
            rgb_backlight_update(value, false);
        }
        // Note: Backlight is separate from RGB LED on MCP23017
    }
}

void furi_hal_light_blink_start(Light light, uint8_t brightness, uint16_t on_time, uint16_t period) {
    // MCP23017 doesn't support hardware blinking, so just turn LED on at max brightness
    UNUSED(brightness);
    UNUSED(on_time);
    UNUSED(period);
    furi_hal_light_set(light, 0xFF);
}

void furi_hal_light_blink_stop(void) {
    // Stop blinking by turning LED off
    furi_hal_mcp23017_led_off();
}

void furi_hal_light_blink_set_color(Light light) {
    // For MCP23017, just set the LED color (on/off)
    bool red = (light & LightRed) != 0;
    bool green = (light & LightGreen) != 0;
    bool blue = (light & LightBlue) != 0;
    furi_hal_mcp23017_led_set_color(red, green, blue);
}

void furi_hal_light_sequence(const char* sequence) {
    do {
        switch(*sequence) {
        case 'R':
            furi_hal_light_set(LightRed, 0xFF);
            break;
        case 'r':
            furi_hal_light_set(LightRed, 0x00);
            break;
        case 'G':
            furi_hal_light_set(LightGreen, 0xFF);
            break;
        case 'g':
            furi_hal_light_set(LightGreen, 0x00);
            break;
        case 'B':
            furi_hal_light_set(LightBlue, 0xFF);
            break;
        case 'b':
            furi_hal_light_set(LightBlue, 0x00);
            break;
        case 'W':
            furi_hal_light_set(LightBacklight, 0xFF);
            break;
        case 'w':
            furi_hal_light_set(LightBacklight, 0x00);
            break;
        case '.':
            furi_delay_ms(250);
            break;
        case '-':
            furi_delay_ms(500);
            break;
        default:
            break;
        }
        sequence++;
    } while(*sequence != 0);
}
