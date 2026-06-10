#include <furi.h>
#include <furi_hal_i2c.h>
#include <stdio.h>

int32_t ina219_scan_main(void* context) {
    UNUSED(context);
    FURI_LOG_I("INA219Scan", "Starting I2C scan");

    // Helper function to scan a bus handle (C version)
    void scan_bus(const FuriHalI2cBusHandle* handle, const char* name) {
        FURI_LOG_I("INA219Scan", "Scanning bus %s", name);
        furi_hal_i2c_acquire(handle);
        for(uint8_t addr = 0x03; addr <= 0x77; ++addr) {
            bool ready = furi_hal_i2c_is_device_ready(handle, addr, 50);
            FURI_LOG_I("INA219Scan", "%s Addr 0x%02X -> %s", name, addr, ready ? "ACK" : "NOACK");
        }
        // Try reading a few registers at 0x40
        uint16_t regv;
        bool ok = furi_hal_i2c_read_reg_16(handle, 0x40, 0x00, &regv, 200);
        FURI_LOG_I("INA219Scan", "%s read CONFIG(0x40) -> %s (0x%04X)", name, ok ? "OK" : "FAIL", regv);
        ok = furi_hal_i2c_read_reg_16(handle, 0x40, 0x02, &regv, 200);
        FURI_LOG_I("INA219Scan", "%s read BUS_VOLTAGE(0x40) -> %s (0x%04X)", name, ok ? "OK" : "FAIL", regv);
        ok = furi_hal_i2c_read_reg_16(handle, 0x40, 0x04, &regv, 200);
        FURI_LOG_I("INA219Scan", "%s read CURRENT(0x40) -> %s (0x%04X)", name, ok ? "OK" : "FAIL", regv);
        furi_hal_i2c_release(handle);
    }

    // Scan both power and external I2C buses to find device location
    scan_bus(&furi_hal_i2c_handle_power, "POWER");
    scan_bus(&furi_hal_i2c_handle_external, "EXTERNAL");

    FURI_LOG_I("INA219Scan", "Scan complete");
    return 0;
}

// App entry
int main(void) {
    ina219_scan_main(NULL);
    return 0;
}
