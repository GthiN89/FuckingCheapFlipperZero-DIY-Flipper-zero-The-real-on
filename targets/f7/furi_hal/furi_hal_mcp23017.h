#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "furi_hal_gpio.h"
#include "furi_hal_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Mcp23017IntCallback)(void* ctx);

// Set which I2C bus to use (power or external). Call BEFORE init().
// Default is I2C1 (furi_hal_i2c_handle_power) for early boot compatibility.
// Call this to switch to I2C3: furi_hal_mcp23017_set_i2c_bus(&furi_hal_i2c_handle_external);
void furi_hal_mcp23017_set_i2c_bus(const FuriHalI2cBusHandle* bus_handle);

// Initialize MCP23017 at given I2C address (7-bit)
// Note: public no-arg API maintained for compatibility.
bool furi_hal_mcp23017_init(void);
// Internal: initialize with explicit address (not exported)
bool furi_hal_mcp23017_init_ex(uint8_t i2c_addr);

// Read GPIOA and GPIOB registers (16 bits)
bool furi_hal_mcp23017_read_gpio(uint16_t* gpio_state);

// Configure IOCON / interrupt registers as minimal convenience
// gpios_to_input_mask: lower 8 bits = GPIOA mask, upper 8 bits = GPIOB mask
bool furi_hal_mcp23017_configure_interrupts(uint16_t gpios_to_input_mask);

// Attach callback for INT pin (STM32 pin) - the code will call this when INT line triggers
void furi_hal_mcp23017_attach_int(GpioExtiCallback cb, void* ctx);

// Called by board EXTI handler to propagate interrupt from MCP23017
void furi_hal_mcp23017_handle_int(void);

// Write single MCP23017 pin (0-15). Pins 0-7 = GPIOA, 8-15 = GPIOB
// Returns true on success.
bool furi_hal_mcp23017_write_pin(uint8_t pin, bool value);

// Write full 16-bit GPIO state (lower=GPIOA, upper=GPIOB)
bool furi_hal_mcp23017_write_gpio(uint16_t gpio_state);

// Set pin direction: true = input, false = output. Pins 0-7 = GPIOA, 8-15 = GPIOB
bool furi_hal_mcp23017_set_pin_direction(uint8_t pin, bool is_input);

// RGB LED control functions - Pins B1=RED(9), B2=GREEN(10), B3=BLUE(11)
// Initialize RGB LED pins as outputs
bool furi_hal_mcp23017_led_init(void);

// Control individual LED colors (on/off only)
bool furi_hal_mcp23017_led_set_red(bool on);
bool furi_hal_mcp23017_led_set_green(bool on);
bool furi_hal_mcp23017_led_set_blue(bool on);

// Set all three colors at once
bool furi_hal_mcp23017_led_set_color(bool red, bool green, bool blue);

// Set LED with brightness value (0x00=off, 0xFF=on, >0x7F=on)
bool furi_hal_mcp23017_led_set(uint8_t red, uint8_t green, uint8_t blue);

// Turn all LEDs off
bool furi_hal_mcp23017_led_off(void);

#ifdef __cplusplus
}
#endif
