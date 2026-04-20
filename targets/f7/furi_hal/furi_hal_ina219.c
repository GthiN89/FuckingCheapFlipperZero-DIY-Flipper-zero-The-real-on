#include "furi_hal_ina219.h"
#include <furi_hal_i2c.h>
#include <furi.h>
#include <stdint.h>
#include <string.h>

// INA219 default I2C address (0x40)
#define INA219_I2C_ADDR_BASE 0x40

// INA219 register addresses
#define INA219_REG_CONFIG 0x00
#define INA219_REG_SHUNT_VOLTAGE 0x01
#define INA219_REG_BUS_VOLTAGE 0x02
#define INA219_REG_POWER 0x03
#define INA219_REG_CURRENT 0x04
#define INA219_REG_CALIBRATION 0x05

// Default shunt resistor value in ohms (board-specific)
#ifndef INA219_SHUNT_OHMS
#define INA219_SHUNT_OHMS 0.1f
#endif

static bool s_detected = false;
static uint8_t s_address = INA219_I2C_ADDR_BASE;

// Helper: read 16-bit register (big endian as INA219 returns MSB first)
static bool ina219_read_reg16(uint8_t reg, uint16_t* out) {
    uint16_t addr8 = ((uint16_t)s_address) << 1;
    FURI_LOG_D("INA219", "[READ] Reg: 0x%02X, Addr: 0x%02X", reg, s_address);
    const FuriHalI2cBusHandle* handle = &furi_hal_i2c_handle_external;
    bool ok = furi_hal_i2c_read_reg_16(handle, (uint8_t)addr8, reg, out, 200);
    if (!ok) {
        FURI_LOG_E("INA219", "[READ FAIL] Reg: 0x%02X, Addr: 0x%02X", reg, s_address);
    } else {
        FURI_LOG_D("INA219", "[READ OK] Reg: 0x%02X, Value: 0x%04X", reg, *out);
    }
    return ok;
}

// Helper: write 16-bit register
static bool ina219_write_reg16(uint8_t reg, uint16_t val) {
    uint16_t addr8 = ((uint16_t)s_address) << 1;
    FURI_LOG_D("INA219", "[WRITE] Reg: 0x%02X, Addr: 0x%02X, Value: 0x%04X", reg, s_address, val);
    const FuriHalI2cBusHandle* handle = &furi_hal_i2c_handle_external;
    bool ok = furi_hal_i2c_write_reg_16(handle, (uint8_t)addr8, reg, val, 200);
    if (!ok) {
        FURI_LOG_E("INA219", "[WRITE FAIL] Reg: 0x%02X, Addr: 0x%02X", reg, s_address);
    }
    return ok;
}

// Dump all INA219 registers for debugging
static void ina219_dump_registers() {
    uint16_t regs[6] = {0};
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    ina219_read_reg16(INA219_REG_CONFIG, &regs[0]);
    ina219_read_reg16(INA219_REG_SHUNT_VOLTAGE, &regs[1]);
    ina219_read_reg16(INA219_REG_BUS_VOLTAGE, &regs[2]);
    ina219_read_reg16(INA219_REG_POWER, &regs[3]);
    ina219_read_reg16(INA219_REG_CURRENT, &regs[4]);
    ina219_read_reg16(INA219_REG_CALIBRATION, &regs[5]);
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    FURI_LOG_I(
        "INA219",
        "Regs: CONFIG=0x%04X, SHUNT=0x%04X, BUS=0x%04X, POWER=0x%04X, CURRENT=0x%04X, CAL=0x%04X",
        regs[0], regs[1], regs[2], regs[3], regs[4], regs[5]);
}

// Configure INA219 for 32V bus, 320mV shunt, 12-bit, continuous mode
static bool ina219_configure() {
    // Default config: 32V bus, 320mV shunt, 12-bit, continuous
    uint16_t config = 0x399F;
    FURI_LOG_I("INA219", "Configuring INA219 with 0x%04X", config);
    bool ok = ina219_write_reg16(INA219_REG_CONFIG, config);
    if (!ok) {
        FURI_LOG_E("INA219", "Failed to write config");
        return false;
    }

    // Verify config was written
    uint16_t config_read = 0;
    ok = ina219_read_reg16(INA219_REG_CONFIG, &config_read);
    if (!ok || config_read != config) {
        FURI_LOG_E("INA219", "Config mismatch: wrote 0x%04X, read 0x%04X", config, config_read);
        return false;
    }

    // Calibrate for 0.1Ω shunt, 320mV range, 12-bit
    uint16_t calibration = 4096; // Default calibration value
    ok = ina219_write_reg16(INA219_REG_CALIBRATION, calibration);
    if (!ok) {
        FURI_LOG_E("INA219", "Failed to write calibration");
        return false;
    }

    // Verify calibration
    uint16_t calibration_read = 0;
    ok = ina219_read_reg16(INA219_REG_CALIBRATION, &calibration_read);
    if (!ok || calibration_read != calibration) {
        FURI_LOG_E("INA219", "Calibration mismatch: wrote 0x%04X, read 0x%04X", calibration, calibration_read);
        return false;
    }

    FURI_LOG_I("INA219", "Configuration and calibration successful");
    return true;
}

bool furi_hal_ina219_init(void) {
    uint16_t cfg;
    bool ok = false;
    FURI_LOG_I("INA219", "Initializing INA219...");
    furi_delay_ms(200); // Give peripherals time to stabilize

    const int max_attempts = 3;
    for (int attempt = 0; attempt < max_attempts && !ok; ++attempt) {
        if (attempt > 0) {
            FURI_LOG_I("INA219", "Retrying INA219 scan (attempt %d/%d)", attempt + 1, max_attempts);
            furi_delay_ms(100);
        }
        for (uint8_t a = INA219_I2C_ADDR_BASE; a <= (INA219_I2C_ADDR_BASE | 0x0F); ++a) {
            s_address = a;
            FURI_LOG_D("INA219", "Probing address: 0x%02X", s_address);
            furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
            bool ready = furi_hal_i2c_is_device_ready(&furi_hal_i2c_handle_external, s_address, 100);
            FURI_LOG_D("INA219", "is_device_ready(0x%02X) -> %s", s_address, ready ? "ACK" : "NOACK");

            uint8_t addr7 = s_address;
            uint8_t addr8 = (uint8_t)(s_address << 1);
            uint16_t cfg_read = 0;
            bool read7 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_external, addr7, INA219_REG_CONFIG, &cfg_read, 200);
            if (read7) {
                ok = true;
                FURI_LOG_I("INA219", "Detected INA219 at 0x%02X (7-bit read)", s_address);
                furi_hal_i2c_release(&furi_hal_i2c_handle_external);
                break;
            }
            uint16_t cfg_read8 = 0;
            bool read8 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_external, addr8, INA219_REG_CONFIG, &cfg_read8, 200);
            if (read8) {
                ok = true;
                s_address = (uint8_t)(addr8 >> 1);
                FURI_LOG_I("INA219", "Detected INA219 at 0x%02X (8-bit read)", s_address);
                furi_hal_i2c_release(&furi_hal_i2c_handle_external);
                break;
            }
            furi_hal_i2c_release(&furi_hal_i2c_handle_external);
        }
    }

    if (!ok) {
        FURI_LOG_E("INA219", "INA219 not detected. Attempting calibration write...");
        s_address = INA219_I2C_ADDR_BASE;
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
        ina219_write_reg16(INA219_REG_CALIBRATION, 4096);
        ok = ina219_read_reg16(INA219_REG_CONFIG, &cfg);
        furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    }

    if (ok) {
        // Configure and calibrate INA219
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
        ok = ina219_configure();
        furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    }

    if (!ok) {
        FURI_LOG_E("INA219", "Diagnostic: Direct read attempts at 0x%02X", INA219_I2C_ADDR_BASE);
        uint8_t probe_addr8 = (uint8_t)(INA219_I2C_ADDR_BASE << 1);
        furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
        bool r1 = furi_hal_i2c_is_device_ready(&furi_hal_i2c_handle_external, INA219_I2C_ADDR_BASE, 200);
        FURI_LOG_I("INA219", "is_device_ready(0x%02X) -> %s", INA219_I2C_ADDR_BASE, r1 ? "ACK" : "NOACK");
        uint16_t cfg2 = 0;
        bool r2 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_external, probe_addr8, INA219_REG_CONFIG, &cfg2, 500);
        FURI_LOG_I("INA219", "read_reg_16 CONFIG(0x%02X) -> %s (0x%04X)", INA219_I2C_ADDR_BASE, r2 ? "OK" : "FAIL", cfg2);
        uint16_t bv = 0;
        bool r3 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_external, probe_addr8, INA219_REG_BUS_VOLTAGE, &bv, 500);
        FURI_LOG_I("INA219", "read_reg_16 BUS_VOLTAGE(0x%02X) -> %s (0x%04X)", INA219_I2C_ADDR_BASE, r3 ? "OK" : "FAIL", bv);
        uint16_t cur = 0;
        bool r4 = furi_hal_i2c_read_reg_16(&furi_hal_i2c_handle_external, probe_addr8, INA219_REG_CURRENT, &cur, 500);
        FURI_LOG_I("INA219", "read_reg_16 CURRENT(0x%02X) -> %s (0x%04X)", INA219_I2C_ADDR_BASE, r4 ? "OK" : "FAIL", cur);
        furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    }

    s_detected = ok;
    if (s_detected) {
        FURI_LOG_I("INA219", "INA219 initialized successfully at 0x%02X", s_address);
        ina219_dump_registers(); // Dump registers after init
    } else {
        FURI_LOG_E("INA219", "INA219 not detected on I2C bus");
    }
    return s_detected;
}

bool furi_hal_ina219_is_ready(void) {
    FURI_LOG_D("INA219", "is_ready() -> %s", s_detected ? "true" : "false");
    return s_detected;
}

bool furi_hal_ina219_get_voltage_current(float* voltage_v, float* current_a) {
    if(!voltage_v || !current_a) return false;
    if(!s_detected) return false;

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    uint16_t bus_raw = 0;
    uint16_t shunt_raw = 0;
    bool ok1 = ina219_read_reg16(INA219_REG_BUS_VOLTAGE, &bus_raw);
    bool ok2 = ina219_read_reg16(INA219_REG_SHUNT_VOLTAGE, &shunt_raw);
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    FURI_LOG_D("INA219", "Bus raw: 0x%04X, Shunt raw: 0x%04X", bus_raw, shunt_raw);

    if(!ok1 && !ok2) return false;

    // Bus voltage register: bits [15:3] are voltage in 4mV LSB
    float voltage = 0.0f;
    if(ok1) {
        uint16_t v = (uint16_t)(bus_raw >> 3);
        voltage = (float)v * 0.004f; // 4 mV per bit
        FURI_LOG_D("INA219", "Bus voltage: %.3fV", (double)voltage);
    }

    // Compute current from shunt voltage register (LSB = 10uV)
    float current = 0.0f;
    if(ok2) {
        int16_t s = (int16_t)shunt_raw; // signed 16-bit
        float shunt_v = (float)s * 10e-6f; // volts
        current = shunt_v / INA219_SHUNT_OHMS;
        current = -current; // invert sign if needed
        FURI_LOG_D("INA219", "Shunt voltage: %.6fV, Current: %.3fA", (double)shunt_v, (double)current);
    }

    *voltage_v = voltage;
    *current_a = current;
    return true;
}