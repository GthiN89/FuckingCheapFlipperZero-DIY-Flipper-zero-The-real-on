/* Minimal INA219 HAL wrapper for Furi HAL
 * Provides simple voltage/current read support using the platform I2C HAL.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize INA219 wrapper. Returns true if INA219 device detected. */
bool furi_hal_ina219_init(void);

/** Returns true if INA219 appears ready (detected). */
bool furi_hal_ina219_is_ready(void);

/** Read bus voltage (V) and current (A) from INA219.
 *  Returns true if a valid reading was obtained. */
bool furi_hal_ina219_get_voltage_current(float* voltage_v, float* current_a);

#ifdef __cplusplus
}
#endif
