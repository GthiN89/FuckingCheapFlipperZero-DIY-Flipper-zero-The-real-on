#include "furi_hal_ina219.h"
#include <furi_hal_i2c.h>
#include <furi.h>
#include <stdint.h>
#include <string.h>

// INA219 default I2C address (0x40)
// Default INA219 base address; device may respond at 0x40..0x4F depending on pins
#define INA219_I2C_ADDR_BASE 0x40

// INA219 register addresses
#define INA219_REG_CONFIG 0x00
#define INA219_REG_SHUNT_VOLTAGE 0x01
#define INA219_REG_BUS_VOLTAGE 0x02
#define INA219_REG_POWER 0x03
#define INA219_REG_CURRENT 0x04
#define INA219_REG_CALIBRATION 0x05

// Default shunt resistor value in ohms (board-specific). Override if needed.
#ifndef INA219_SHUNT_OHMS
#define INA219_SHUNT_OHMS 0.1f
#endif

static bool s_detected = false;
static uint8_t s_address = INA219_I2C_ADDR_BASE;

// Helper: read 16-bit register (big endian as INA219 returns MSB first)
static bool ina219_read_reg16(uint8_t reg, uint16_t* out) {
    uint16_t addr8 = ((uint16_t)s_address) << 1; // match INA driver which uses addr<<1
    // NOTE: callers MUST acquire the I2C bus handle before calling this function.
    // Use external handle (matches existing INA driver behavior). Do not attempt
    // to call I2C helpers on another bus without acquiring it — that triggers
    // furi_check and crashes during init.
    const FuriHalI2cBusHandle* handle = &furi_hal_i2c_handle_power;
    bool ok = furi_hal_i2c_read_reg_16(handle, (uint8_t)addr8, reg, out, 200);
    return ok;
}

// Helper: write 16-bit register
static bool ina219_write_reg16(uint8_t reg, uint16_t val) {
    uint16_t addr8 = ((uint16_t)s_address) << 1;
    // NOTE: callers MUST acquire the I2C bus handle before calling this function.
    const FuriHalI2cBusHandle* handle = &furi_hal_i2c_handle_power;
    bool ok = furi_hal_i2c_write_reg_16(handle, (uint8_t)addr8, reg, val, 200);
    return ok;
}

bool furi_hal_ina219_init(void) {
    // Check presence by reading CONFIG reg
    uint16_t cfg;
    bool ok = false;
    // Scan possible INA219 addresses (0x40..0x4F)
    // Give peripherals more time to stabilise (some boards need longer)
    furi_delay_ms(200);

    const int max_attempts = 3;
    for(int attempt = 0; attempt < max_attempts && !ok; ++attempt) {
        if(attempt > 0) {
            FURI_LOG_I("FuriHalINA219", "Retrying INA219 scan (attempt %d/%d)", attempt + 1, max_attempts);
            furi_delay_ms(100);
        }
        for(uint8_t a = INA219_I2C_ADDR_BASE; a <= (INA219_I2C_ADDR_BASE | 0x0F); ++a) {
            s_address = a;
            // Acquire external bus (like app driver) before checking readiness
            furi_hal_i2c_acquire(&furi_hal_i2c_handle_power);
            // Probe using 7-bit address for is_device_ready (no verbose log)
            furi_hal_i2c_is_device_ready(&furi_hal_i2c_handle_power, s_address, 100);
            // Also try direct register read probes in both address formats to detect
            // devices that may respond only to particular transaction styles.
            uint8_t addr7 = s_address;
            uint8_t addr8 = (uint8_t)(s_address << 1);
            uint16_t cfg_read = 0;
            bool read7 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_power, addr7, INA219_REG_CONFIG, &cfg_read, 200);
            if(read7) {
                ok = true;
                // Detected via 7-bit read
                furi_hal_i2c_release(&furi_hal_i2c_handle_power);
                FURI_LOG_I("FuriHalINA219", "Detected INA219 at 0x%02X", s_address);
                break;
            }
            uint16_t cfg_read8 = 0;
            bool read8 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_power, addr8, INA219_REG_CONFIG, &cfg_read8, 200);
            if(read8) {
                ok = true;
                s_address = (uint8_t)(addr8 >> 1); // normalize to 7-bit
                furi_hal_i2c_release(&furi_hal_i2c_handle_power);
                FURI_LOG_I("FuriHalINA219", "Detected INA219 (via 8bit read) at normalized 0x%02X", s_address);
                break;
            }
            furi_hal_i2c_release(&furi_hal_i2c_handle_power);
        }
    }

    if(!ok) {
        // Try a simple calibration write at the default address and re-check
        s_address = INA219_I2C_ADDR_BASE;
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_power);
        ina219_write_reg16(INA219_REG_CALIBRATION, 4096);
        ok = ina219_read_reg16(INA219_REG_CONFIG, &cfg);
        furi_hal_i2c_release(&furi_hal_i2c_handle_power);
    }

    if(!ok) {
        // Extra diagnostic: try direct reads with longer timeouts and log results
        FURI_LOG_I("FuriHalINA219", "Diagnostic: direct read attempts at 0x%02X", INA219_I2C_ADDR_BASE);
        uint8_t probe_addr8 = (uint8_t)(INA219_I2C_ADDR_BASE << 1);
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_power);
        bool r1 = furi_hal_i2c_is_device_ready(&furi_hal_i2c_handle_power, INA219_I2C_ADDR_BASE, 200);
        FURI_LOG_I("FuriHalINA219", "is_device_ready(0x%02X) -> %s", INA219_I2C_ADDR_BASE, r1 ? "ACK" : "NOACK");
        uint16_t cfg2 = 0;
        bool r2 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_power, probe_addr8, INA219_REG_CONFIG, &cfg2, 500);
        FURI_LOG_I("FuriHalINA219", "read_reg_16 CONFIG(0x%02X<<1 -> 0x%02X) -> %s (0x%04X)", INA219_I2C_ADDR_BASE, probe_addr8, r2 ? "OK" : "FAIL", cfg2);
        uint16_t bv = 0;
        bool r3 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_power, probe_addr8, INA219_REG_BUS_VOLTAGE, &bv, 500);
        FURI_LOG_I("FuriHalINA219", "read_reg_16 BUS_VOLTAGE(0x%02X<<1 -> 0x%02X) -> %s (0x%04X)", INA219_I2C_ADDR_BASE, probe_addr8, r3 ? "OK" : "FAIL", bv);
        uint16_t cur = 0;
        bool r4 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_power, probe_addr8, INA219_REG_CURRENT, &cur, 500);
        FURI_LOG_I("FuriHalINA219", "read_reg_16 CURRENT(0x%02X<<1 -> 0x%02X) -> %s (0x%04X)", INA219_I2C_ADDR_BASE, probe_addr8, r4 ? "OK" : "FAIL", cur);
        furi_hal_i2c_release(&furi_hal_i2c_handle_power);
        (void)r1; (void)r2; (void)r3; (void)r4;
    }
    
    s_detected = ok;
    if(!s_detected) FURI_LOG_I("FuriHalINA219", "INA219 not detected on I2C bus");
    return s_detected;
}

bool furi_hal_ina219_is_ready(void) {
    return s_detected;
}

bool furi_hal_ina219_get_voltage_current(float* voltage_v, float* current_a) {
    if(!voltage_v || !current_a) return false;
    if(!s_detected) return false;

    // Acquire external bus (matches app driver)
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_power);
    uint16_t bus_raw = 0;
    uint16_t shunt_raw = 0;
    bool ok1 = ina219_read_reg16(INA219_REG_BUS_VOLTAGE, &bus_raw);
    bool ok2 = ina219_read_reg16(INA219_REG_SHUNT_VOLTAGE, &shunt_raw);
    furi_hal_i2c_release(&furi_hal_i2c_handle_power);

    if(!ok1 && !ok2) return false;

    // Bus voltage register: bits [15:3] are voltage in 4mV LSB
    float voltage = 0.0f;
    if(ok1) {
        uint16_t v = (uint16_t)(bus_raw >> 3);
        voltage = (float)v * 0.004f; // 4 mV per bit
    }

    // Compute current from shunt voltage register (LSB = 10uV), avoids reliance
    // on INA219 calibration/current register which may not be set.
    float current = 0.0f;
    if(ok2) {
        int16_t s = (int16_t)shunt_raw; // signed 16-bit
        // shunt voltage LSB = 10uV
        float shunt_v = (float)s * 10e-6f; // volts
        // current = V_shunt / R_shunt
        current = shunt_v / INA219_SHUNT_OHMS;
        // invert sign to match board wiring if needed (kept inverted as earlier)
        current = -current;
    }

    *voltage_v = voltage;
    *current_a = current;
    return true;
}
