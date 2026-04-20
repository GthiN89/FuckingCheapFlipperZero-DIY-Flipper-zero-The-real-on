#include "furi_hal_mcp23017.h"
#include "furi_hal_i2c.h"
#include "furi_hal_gpio.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <furi.h>

// MCP23017 registers
#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPIOA  0x12
#define MCP_GPIOB  0x13
#define MCP_GPINTENA 0x04
#define MCP_GPINTENB 0x05
#define MCP_DEFVALA 0x06
#define MCP_DEFVALB 0x07
#define MCP_INTCONA 0x08
#define MCP_INTCONB 0x09
#define MCP_IOCON 0x0A

#define TAG "FuriHalMCP23017"

static uint8_t mcp_addr = 0x20; // default 7-bit
// If true, the driver should use the 8-bit address form (addr<<1) when talking
// to the device. Some environments/devices require the 8-bit form for low-level
// register ops. This is discovered during probe and then cached.
static bool mcp_use_8bit_addr = false;
// I2C bus handle - configurable for either power or external bus
// Default to power bus (I2C1) as it's initialized earlier in boot
// Call furi_hal_mcp23017_set_i2c_bus() to switch to external (I2C3)
static const FuriHalI2cBusHandle* mcp_i2c_handle = &furi_hal_i2c_handle_power;
static GpioExtiCallback exti_cb = NULL;
static void* exti_ctx = NULL;

static bool mcp_write_reg(uint8_t reg, uint8_t val);
static bool mcp_read_reg(uint8_t reg, uint8_t* val);
static bool mcp_write_reg_locked(uint8_t reg, uint8_t val);
static bool mcp_write_reg_locked_addr(uint8_t addr, uint8_t reg, uint8_t val);

// Set which I2C bus to use (power or external). Call this before init().
void furi_hal_mcp23017_set_i2c_bus(const FuriHalI2cBusHandle* bus_handle) {
    furi_check(bus_handle != NULL);
    mcp_i2c_handle = bus_handle;
    FURI_LOG_I(TAG, "MCP23017 I2C bus set");
}

// Internal implementation that accepts explicit I2C address
bool furi_hal_mcp23017_init_ex(uint8_t i2c_addr) {
    // Accept explicit I2C address and store as normalized 7-bit form
    mcp_addr = i2c_addr;
    FURI_LOG_I(TAG, "Initializing MCP23017 at I2C address 0x%02X on I2C bus", mcp_addr);
    // Acquire bus and probe device similarly to INA219 driver
    furi_hal_i2c_acquire(mcp_i2c_handle);
    bool detected = false;
    // Quick device ready probe using 7-bit address
    if(furi_hal_i2c_is_device_ready(mcp_i2c_handle, mcp_addr, 100)) {
        detected = true;
    } else {
        // Try direct register read probes using both 7-bit and 8-bit address forms
        uint8_t probe = 0;
        if(furi_hal_i2c_read_reg_8(mcp_i2c_handle, mcp_addr, MCP_IOCON, &probe, 200)) {
            detected = true;
        } else {
            uint8_t probe8 = (uint8_t)(mcp_addr << 1);
            if(furi_hal_i2c_read_reg_8(mcp_i2c_handle, probe8, MCP_IOCON, &probe, 200)) {
                detected = true;
            }
        }
    }

    if(!detected) {
        furi_hal_i2c_release(mcp_i2c_handle);
        FURI_LOG_E(TAG, "MCP23017 not detected at 0x%02X", mcp_addr);
        return false;
    }

    // set IOCON: Mirror interrupts, active-low open-drain (common config)
    bool io_ok = false;
    // Try a few times with 7-bit address then 8-bit address form
    for(int attempt = 0; attempt < 3 && !io_ok; ++attempt) {
        if(mcp_write_reg_locked(MCP_IOCON, 0x44)) {
            io_ok = true;
            break;
        }
        // try 8-bit address form
        uint8_t addr8 = (uint8_t)(mcp_addr << 1);
        if(mcp_write_reg_locked_addr(addr8, MCP_IOCON, 0x44)) {
            io_ok = true;
            // remember that the device expects 8-bit address form
            mcp_use_8bit_addr = true;
            break;
        }
        furi_delay_ms(10);
    }

    if(!io_ok) {
        FURI_LOG_E(TAG, "Failed to write IOCON");
        furi_hal_i2c_release(&furi_hal_i2c_handle_power);
        return false; // MIRROR=1, INTPOL=0, ODR=1
    }
    furi_hal_i2c_release(mcp_i2c_handle);
    FURI_LOG_I(TAG, "MCP23017 initialized");
    return true;
}

// Public no-argument wrapper kept for API compatibility. Calls the
// explicit-address implementation with the default address 0x27.
bool furi_hal_mcp23017_init(void) {
    return furi_hal_mcp23017_init_ex(mcp_addr);
}

static bool mcp_write_reg(uint8_t reg, uint8_t val) {
    bool ret = false;
    furi_hal_i2c_acquire(mcp_i2c_handle);
    if(mcp_use_8bit_addr) {
        uint8_t addr8 = (uint8_t)(mcp_addr << 1);
        ret = furi_hal_i2c_write_reg_8(mcp_i2c_handle, addr8, reg, val, 200);
    } else {
        ret = furi_hal_i2c_write_reg_8(mcp_i2c_handle, mcp_addr, reg, val, 200);
    }
    furi_hal_i2c_release(mcp_i2c_handle);
    return ret;
}

static bool mcp_read_reg(uint8_t reg, uint8_t* val) {
    bool ret = false;
    furi_hal_i2c_acquire(mcp_i2c_handle);
    if(mcp_use_8bit_addr) {
        uint8_t addr8 = (uint8_t)(mcp_addr << 1);
        ret = furi_hal_i2c_read_reg_8(mcp_i2c_handle, addr8, reg, val, 200);
    } else {
        ret = furi_hal_i2c_read_reg_8(mcp_i2c_handle, mcp_addr, reg, val, 200);
    }
    furi_hal_i2c_release(mcp_i2c_handle);
    return ret;
}

static bool mcp_write_reg_locked(uint8_t reg, uint8_t val) {
    // Caller must hold the I2C bus
    if(mcp_use_8bit_addr) {
        uint8_t addr8 = (uint8_t)(mcp_addr << 1);
        return furi_hal_i2c_write_reg_8(mcp_i2c_handle, addr8, reg, val, 200);
    }
    return furi_hal_i2c_write_reg_8(mcp_i2c_handle, mcp_addr, reg, val, 200);
}

static bool mcp_write_reg_locked_addr(uint8_t addr, uint8_t reg, uint8_t val) {
    return furi_hal_i2c_write_reg_8(mcp_i2c_handle, addr, reg, val, 200);
}

// mcp_read_reg_locked removed (not needed). Use mcp_read_reg which acquires/releases bus.

bool furi_hal_mcp23017_read_gpio(uint16_t* gpio_state) {
    uint8_t a,b;
    if(!mcp_read_reg(MCP_GPIOA, &a)) return false;
    if(!mcp_read_reg(MCP_GPIOB, &b)) return false;
    *gpio_state = (uint16_t)a | ((uint16_t)b << 8);
    return true;
}

bool furi_hal_mcp23017_configure_interrupts(uint16_t gpios_to_input_mask) {
    FURI_LOG_I(TAG, "Configuring MCP23017 interrupts with mask 0x%04X", gpios_to_input_mask);
    // Configure direction: 1=input, 0=output
    uint8_t mask_a = (uint8_t)(gpios_to_input_mask & 0xFF);
    uint8_t mask_b = (uint8_t)((gpios_to_input_mask >> 8) & 0xFF);
    if(!mcp_write_reg(MCP_IODIRA, mask_a)) {
        FURI_LOG_I(TAG, "Failed to write IODIRA");
        return false;
    }
    if(!mcp_write_reg(MCP_IODIRB, mask_b)) {
        FURI_LOG_I(TAG, "Failed to write IODIRB");
        return false;
    }
    // enable interrupt-on-change for those pins
    if(!mcp_write_reg(MCP_GPINTENA, mask_a)) {
        FURI_LOG_I(TAG, "Failed to write GPINTENA");
        return false;
    }
    if(!mcp_write_reg(MCP_GPINTENB, mask_b)) {
        FURI_LOG_I(TAG, "Failed to write GPINTENB");
        return false;
    }

    // Enable internal pull-ups for the input pins so buttons wired to ground
    // see defined levels (typical active-low wiring). Write GPPUA/GPPUB.
    // GPPUA = 0x0C, GPPUB = 0x0D
    if(!mcp_write_reg(0x0C, mask_a)) {
        FURI_LOG_I(TAG, "Failed to write GPPUA (pull-ups)");
        return false;
    }
    if(!mcp_write_reg(0x0D, mask_b)) {
        FURI_LOG_I(TAG, "Failed to write GPPUB (pull-ups)");
        return false;
    }

    // set interrupt control to compare to previous (0)
    if(!mcp_write_reg(MCP_INTCONA, 0x00)) {
        FURI_LOG_I(TAG, "Failed to write INTCONA");
        return false;
    }
    if(!mcp_write_reg(MCP_INTCONB, 0x00)) {
        FURI_LOG_I(TAG, "Failed to write INTCONB");
        // Dump registers for diagnostics
        uint8_t ra, rb, ga, gb, iocon;
        mcp_read_reg(MCP_IODIRA, &ra);
        mcp_read_reg(MCP_IODIRB, &rb);
        mcp_read_reg(MCP_GPINTENA, &ga);
        mcp_read_reg(MCP_GPINTENB, &gb);
        mcp_read_reg(MCP_IOCON, &iocon);
        FURI_LOG_E(TAG, "RegDump IODIR A:0x%02X B:0x%02X GPINTENA:0x%02X GPINTENB:0x%02X IOCON:0x%02X", ra, rb, ga, gb, iocon);
        return false;
    }

    // Read back a few registers for diagnostics
    uint8_t ra, rb, ga, gb, pu_a, pu_b, iocon;
    mcp_read_reg(MCP_IODIRA, &ra);
    mcp_read_reg(MCP_IODIRB, &rb);
    mcp_read_reg(MCP_GPINTENA, &ga);
    mcp_read_reg(MCP_GPINTENB, &gb);
    mcp_read_reg(0x0C, &pu_a);
    mcp_read_reg(0x0D, &pu_b);
    mcp_read_reg(MCP_IOCON, &iocon);
    FURI_LOG_I(TAG, "RegDump IODIR A:0x%02X B:0x%02X GPINTENA:0x%02X GPINTENB:0x%02X GPPUA:0x%02X GPPUB:0x%02X IOCON:0x%02X", ra, rb, ga, gb, pu_a, pu_b, iocon);

    FURI_LOG_I(TAG, "MCP23017 interrupts configured");
    return true;
}

void furi_hal_mcp23017_attach_int(GpioExtiCallback cb, void* ctx) {
    exti_cb = cb;
    exti_ctx = ctx;
}

// This function should be called by the board-specific EXTI ISR when the INT pin fires
void furi_hal_mcp23017_handle_int(void) {
    if(exti_cb) exti_cb(exti_ctx);
}

// Write full GPIO state: lower byte = GPIOA, upper byte = GPIOB
bool furi_hal_mcp23017_write_gpio(uint16_t gpio_state) {
    uint8_t a = (uint8_t)(gpio_state & 0xFF);
    uint8_t b = (uint8_t)((gpio_state >> 8) & 0xFF);
    // Write GPIOA then GPIOB
    if(!mcp_write_reg(MCP_GPIOA, a)) return false;
    if(!mcp_write_reg(MCP_GPIOB, b)) return false;
    return true;
}

// Write a single pin (0-15). Pins 0-7 -> A; 8-15 -> B
bool furi_hal_mcp23017_write_pin(uint8_t pin, bool value) {
    if(pin > 15) return false;
    uint8_t reg = (pin < 8) ? MCP_GPIOA : MCP_GPIOB;
    uint8_t bit = (uint8_t)(1u << (pin & 0x7));
    uint8_t cur;
    if(!mcp_read_reg(reg, &cur)) return false;
    if(value) cur |= bit;
    else cur &= (uint8_t)~bit;
    if(!mcp_write_reg(reg, cur)) return false;
    return true;
}

// Set pin direction: true = input, false = output
bool furi_hal_mcp23017_set_pin_direction(uint8_t pin, bool is_input) {
    if(pin > 15) return false;
    uint8_t iodir_reg = (pin < 8) ? MCP_IODIRA : MCP_IODIRB;
    uint8_t bit = (uint8_t)(1u << (pin & 0x7));
    uint8_t cur;
    if(!mcp_read_reg(iodir_reg, &cur)) return false;
    if(is_input) cur |= bit; else cur &= (uint8_t)~bit;
    if(!mcp_write_reg(iodir_reg, cur)) return false;
    return true;
}

// LED control functions - RGB LEDs on MCP23017 port B (B1=RED, B2=GREEN, B3=BLUE)
// Initialize RGB LED pins as outputs and turn them off
bool furi_hal_mcp23017_led_init(void) {
    // Configure B1, B2, B3 as outputs (0 = output)
    // B1 = pin 9, B2 = pin 10, B3 = pin 11
    if(!furi_hal_mcp23017_set_pin_direction(9, false)) return false;
    if(!furi_hal_mcp23017_set_pin_direction(10, false)) return false;
    if(!furi_hal_mcp23017_set_pin_direction(11, false)) return false;
    
    // Turn all LEDs off
    if(!furi_hal_mcp23017_write_pin(9, false)) return false;
    if(!furi_hal_mcp23017_write_pin(10, false)) return false;
    if(!furi_hal_mcp23017_write_pin(11, false)) return false;
    
    FURI_LOG_I(TAG, "RGB LED initialized on MCP23017");
    return true;
}

// Set individual LED colors (on/off only, no PWM)
bool furi_hal_mcp23017_led_set_red(bool on) {
    return furi_hal_mcp23017_write_pin(9, on);
}

bool furi_hal_mcp23017_led_set_green(bool on) {
    return furi_hal_mcp23017_write_pin(10, on);
}

bool furi_hal_mcp23017_led_set_blue(bool on) {
    return furi_hal_mcp23017_write_pin(11, on);
}

// Set all three LED colors at once
bool furi_hal_mcp23017_led_set_color(bool red, bool green, bool blue) {
    if(!furi_hal_mcp23017_led_set_red(red)) return false;
    if(!furi_hal_mcp23017_led_set_green(green)) return false;
    if(!furi_hal_mcp23017_led_set_blue(blue)) return false;
    return true;
}

// Set LED with brightness value (0x00 = off, 0xFF = on)
bool furi_hal_mcp23017_led_set(uint8_t red, uint8_t green, uint8_t blue) {
    // Convert brightness to on/off: anything > 0x7F is considered "on"
    return furi_hal_mcp23017_led_set_color(red > 0x7F, green > 0x7F, blue > 0x7F);
}

// Turn all LEDs off
bool furi_hal_mcp23017_led_off(void) {
    return furi_hal_mcp23017_led_set_color(false, false, false);
}
