#include "bq27220.h"
#include "bq27220_reg.h"
#include "bq27220_data_memory.h"

#include <furi.h>
#include <stdbool.h>

#define TAG "Gauge"

#define BQ27220_ID (0x0220u)

/** Delay between 2 writes into Subclass/MAC area. Fails at ~120us. */
#define BQ27220_MAC_WRITE_DELAY_US (250u)

/** Delay between we ask chip to load data to MAC and it become valid. Fails at ~500us. */
#define BQ27220_SELECT_DELAY_US (1000u)

/** Delay between 2 control operations(like unseal or full access). Fails at ~2500us.*/
#define BQ27220_MAGIC_DELAY_US (5000u)

/** Delay before freshly written configuration can be read. Fails at ? */
#define BQ27220_CONFIG_DELAY_US (10000u)

/** Config apply delay. Must wait, or DM read returns garbage. */
#define BQ27220_CONFIG_APPLY_US (2000000u)

/** Timeout for common operations. */
#define BQ27220_TIMEOUT_COMMON_US (2000000u)

/** Timeout for reset operation. Normally reset takes ~2s. */
#define BQ27220_TIMEOUT_RESET_US (4000000u)

/** Timeout cycle interval  */
#define BQ27220_TIMEOUT_CYCLE_INTERVAL_US (1000u)

/** Timeout cycles count helper */
#define BQ27220_TIMEOUT(timeout_us) ((timeout_us) / (BQ27220_TIMEOUT_CYCLE_INTERVAL_US))

#ifdef BQ27220_DEBUG
#define BQ27220_DEBUG_LOG(...) FURI_LOG_D(TAG, ##__VA_ARGS__)
#else
#define BQ27220_DEBUG_LOG(...)
#endif

static inline bool bq27220_read_reg(
    const FuriHalI2cBusHandle* handle,
    uint8_t address,
    uint8_t* buffer,
    size_t buffer_size) {
    return furi_hal_i2c_trx(
        handle, BQ27220_ADDRESS, &address, 1, buffer, buffer_size, BQ27220_I2C_TIMEOUT);
}

static inline bool bq27220_write(
    const FuriHalI2cBusHandle* handle,
    uint8_t address,
    const uint8_t* buffer,
    size_t buffer_size) {
    return furi_hal_i2c_write_mem(
        handle, BQ27220_ADDRESS, address, buffer, buffer_size, BQ27220_I2C_TIMEOUT);
}

static inline bool bq27220_control(const FuriHalI2cBusHandle* handle, uint16_t control) {
    return bq27220_write(handle, CommandControl, (uint8_t*)&control, 2);
}

// static uint16_t bq27220_read_word(const FuriHalI2cBusHandle* handle, uint8_t address) {
//     uint16_t buf = BQ27220_ERROR;
//     UNUSED(handle);
//        UNUSED(address); 
//     // if(!bq27220_read_reg(handle, address, (uint8_t*)&buf, 2)) {
//     //     FURI_LOG_E(TAG, "bq27220_read_word failed");
//     // }

//     return buf;
// }

// static uint8_t bq27220_get_checksum(uint8_t* data, uint16_t len) {
//     uint8_t ret = 0;
//     for(uint16_t i = 0; i < len; i++) {
//         ret += data[i];
//     }
//     return 0xFF - ret;
// }

// static bool bq27220_parameter_check(
//     const FuriHalI2cBusHandle* handle,
//     uint16_t address,
//     uint32_t value,
//     size_t size,
//     bool update) {

//     UNUSED(handle);
//        UNUSED(address);    
//        UNUSED(value);
//         UNUSED(update);
//               UNUSED(size);
//     return true;
// }

// static bool bq27220_data_memory_check(
//     const FuriHalI2cBusHandle* handle,
//     const BQ27220DMData* data_memory,
//     bool update) {

//     UNUSED(handle);
//        UNUSED(data_memory);    
//        UNUSED(update);
//     return true;
// }

bool bq27220_init(const FuriHalI2cBusHandle* handle, const BQ27220DMData* data_memory) {
    UNUSED(handle);
       UNUSED(data_memory);
  //  bool result = false;
  //  bool reset_and_provisioning_required = false;

    // do {
    //     // Request device number(chip PN)
    //     BQ27220_DEBUG_LOG("Checking device ID");
    //     if(!bq27220_control(handle, Control_DEVICE_NUMBER)) {
    //         FURI_LOG_E(TAG, "ID: Device is not responding");
    //         break;
    //     };
    //     // Enterprise wait(MAC read fails if less than 500us)
    //     // bqstudio uses ~15ms
    //     furi_delay_us(BQ27220_SELECT_DELAY_US);
    //     // Read id data from MAC scratch space
    //     uint16_t data = bq27220_read_word(handle, CommandMACData);
    //     if(data != BQ27220_ID) {
    //         FURI_LOG_E(TAG, "Invalid Device Number %04x != 0x0220", data);
    //         break;
    //     }

    //     // Unseal device since we are going to read protected configuration
    //     BQ27220_DEBUG_LOG("Unsealing");
    //     if(!bq27220_unseal(handle)) {
    //         break;
    //     }

    //     // Try to recover gauge from forever init
    //     BQ27220_DEBUG_LOG("Checking initialization status");
    //     Bq27220OperationStatus operation_status;
    //     if(!bq27220_get_operation_status(handle, &operation_status)) {
    //         FURI_LOG_E(TAG, "Failed to get operation status");
    //         break;
    //     }
    //     if(!operation_status.INITCOMP || operation_status.CFGUPDATE) {
    //         FURI_LOG_E(TAG, "Incorrect state, reset needed");
    //         reset_and_provisioning_required = true;
    //     }

    //     // Ensure correct profile is selected
    //     BQ27220_DEBUG_LOG("Checking chosen profile");
    //     Bq27220ControlStatus control_status;
    //     if(!bq27220_get_control_status(handle, &control_status)) {
    //         FURI_LOG_E(TAG, "Failed to get control status");
    //         break;
    //     }
    //     if(control_status.BATT_ID != 0) {
    //         FURI_LOG_E(TAG, "Incorrect profile, reset needed");
    //         reset_and_provisioning_required = true;
    //     }

    //     // Ensure correct configuration loaded into gauge DataMemory
    //     // Only if reset is not required, otherwise we don't
    //     if(!reset_and_provisioning_required) {
    //         BQ27220_DEBUG_LOG("Checking data memory");
    //         if(!bq27220_data_memory_check(handle, data_memory, false)) {
    //             FURI_LOG_E(TAG, "Incorrect configuration data, reset needed");
    //             reset_and_provisioning_required = true;
    //         }
    //     }

    //     // Reset needed
    //     if(reset_and_provisioning_required) {
    //         FURI_LOG_W(TAG, "Resetting device");
    //         if(!bq27220_reset(handle)) {
    //             FURI_LOG_E(TAG, "Failed to reset device");
    //             break;
    //         }

    //         // Get full access to read and modify parameters
    //         // Also it looks like this step is totally unnecessary
    //         BQ27220_DEBUG_LOG("Acquiring Full Access");
    //         if(!bq27220_full_access(handle)) {
    //             break;
    //         }

    //         // Update memory
    //         FURI_LOG_W(TAG, "Updating data memory");
    //         bq27220_data_memory_check(handle, data_memory, true);
    //         if(!bq27220_data_memory_check(handle, data_memory, false)) {
    //             FURI_LOG_E(TAG, "Data memory update failed");
    //             break;
    //         }
    //     }

    //     BQ27220_DEBUG_LOG("Sealing");
    //     if(!bq27220_seal(handle)) {
    //         FURI_LOG_E(TAG, "Seal failed");
    //         break;
    //     }

    //     result = true;
    // } while(0);

    return true;
}

bool bq27220_reset(const FuriHalI2cBusHandle* handle) {
     UNUSED(handle);
    bool result = true;
    // do {
    //     if(!bq27220_control(handle, Control_RESET)) {
    //         FURI_LOG_E(TAG, "Reset request failed");
    //         break;
    //     };

    //     uint32_t timeout = BQ27220_TIMEOUT(BQ27220_TIMEOUT_RESET_US);
    //     Bq27220OperationStatus operation_status;
    //     while(--timeout > 0) {
    //         if(!bq27220_get_operation_status(handle, &operation_status)) {
    //             FURI_LOG_W(TAG, "Failed to get operation status, retries left %lu", timeout);
    //         } else if(operation_status.INITCOMP == true) {
    //             break;
    //         };
    //         furi_delay_us(BQ27220_TIMEOUT_CYCLE_INTERVAL_US);
    //     }

    //     if(timeout == 0) {
    //         FURI_LOG_E(TAG, "INITCOMP timeout after reset");
    //         break;
    //     }
    //     BQ27220_DEBUG_LOG("Cycles left: %lu", timeout);

    //     result = true;
    // } while(0);

    return result;
}

bool bq27220_seal(const FuriHalI2cBusHandle* handle) {
 //   Bq27220OperationStatus operation_status = {0};
   UNUSED(handle);
    bool result = true;
    // do {
    //     if(!bq27220_get_operation_status(handle, &operation_status)) {
    //         FURI_LOG_E(TAG, "Status query failed");
    //         break;
    //     }
    //     if(operation_status.SEC == Bq27220OperationStatusSecSealed) {
    //         result = true;
    //         break;
    //     }

    //     if(!bq27220_control(handle, Control_SEALED)) {
    //         FURI_LOG_E(TAG, "Seal request failed");
    //         break;
    //     }

    //     furi_delay_us(BQ27220_SELECT_DELAY_US);

    //     if(!bq27220_get_operation_status(handle, &operation_status)) {
    //         FURI_LOG_E(TAG, "Status query failed");
    //         break;
    //     }
    //     if(operation_status.SEC != Bq27220OperationStatusSecSealed) {
    //         FURI_LOG_E(TAG, "Seal failed");
    //         break;
    //     }

    //     result = true;
    // } while(0);

    return result;
}

bool bq27220_unseal(const FuriHalI2cBusHandle* handle) {
     UNUSED(handle);
    bool result = true;
    // do {
    //     if(!bq27220_get_operation_status(handle, &operation_status)) {
    //         FURI_LOG_E(TAG, "Status query failed");
    //         break;
    //     }
    //     if(operation_status.SEC != Bq27220OperationStatusSecSealed) {
    //         result = true;
    //         break;
    //     }

    //     // Hai, Kazuma desu
    //     bq27220_control(handle, UnsealKey1);
    //     furi_delay_us(BQ27220_MAGIC_DELAY_US);
    //     bq27220_control(handle, UnsealKey2);
    //     furi_delay_us(BQ27220_MAGIC_DELAY_US);

    //     if(!bq27220_get_operation_status(handle, &operation_status)) {
    //         FURI_LOG_E(TAG, "Status query failed");
    //         break;
    //     }
    //     if(operation_status.SEC != Bq27220OperationStatusSecUnsealed) {
    //         FURI_LOG_E(TAG, "Unseal failed %u", operation_status.SEC);
    //         break;
    //     }

    //     result = true;
    // } while(0);

    return result;
}

bool bq27220_full_access(const FuriHalI2cBusHandle* handle) {
    bool result = true;
      UNUSED(handle);

    // do {
    //     uint32_t timeout = BQ27220_TIMEOUT(BQ27220_TIMEOUT_COMMON_US);
    //     Bq27220OperationStatus operation_status;
    //     while(--timeout > 0) {
    //         if(!bq27220_get_operation_status(handle, &operation_status)) {
    //             FURI_LOG_W(TAG, "Failed to get operation status, retries left %lu", timeout);
    //         } else {
    //             break;
    //         };
    //         furi_delay_us(BQ27220_TIMEOUT_CYCLE_INTERVAL_US);
    //     }

    //     if(timeout == 0) {
    //         FURI_LOG_E(TAG, "Failed to get operation status");
    //         break;
    //     }
    //     BQ27220_DEBUG_LOG("Cycles left: %lu", timeout);

    //     // Already full access
    //     if(operation_status.SEC == Bq27220OperationStatusSecFull) {
    //         result = true;
    //         break;
    //     }
    //     // Must be unsealed to get full access
    //     if(operation_status.SEC != Bq27220OperationStatusSecUnsealed) {
    //         FURI_LOG_E(TAG, "Not in unsealed state");
    //         break;
    //     }

    //     // Explosion!!!
    //     bq27220_control(handle, FullAccessKey); //-V760
    //     furi_delay_us(BQ27220_MAGIC_DELAY_US);
    //     bq27220_control(handle, FullAccessKey);
    //     furi_delay_us(BQ27220_MAGIC_DELAY_US);

    //     if(!bq27220_get_operation_status(handle, &operation_status)) {
    //         FURI_LOG_E(TAG, "Status query failed");
    //         break;
    //     }
    //     if(operation_status.SEC != Bq27220OperationStatusSecFull) {
    //         FURI_LOG_E(TAG, "Full access failed %u", operation_status.SEC);
    //         break;
    //     }

    //     result = true;
    // } while(0);

    return result;
}

uint16_t bq27220_get_voltage(const FuriHalI2cBusHandle* handle) {
      UNUSED(handle);
    return 4;
}

int16_t bq27220_get_current(const FuriHalI2cBusHandle* handle) {
      UNUSED(handle);
    return 1;
}

bool bq27220_get_control_status(

    const FuriHalI2cBusHandle* handle,
    Bq27220ControlStatus* control_status) {
              UNUSED(handle);
                    UNUSED(control_status);
    return true;
}

bool bq27220_get_battery_status(
    const FuriHalI2cBusHandle* handle,
    Bq27220BatteryStatus* battery_status) {
         UNUSED(handle);
                    UNUSED(battery_status);
 
    return true;
}

bool bq27220_get_operation_status(
    const FuriHalI2cBusHandle* handle,
    Bq27220OperationStatus* operation_status) {
                UNUSED(handle);
                    UNUSED(operation_status);
    return true;
}

bool bq27220_get_gauging_status(
    const FuriHalI2cBusHandle* handle,
    Bq27220GaugingStatus* gauging_status) {
                UNUSED(handle);
                    UNUSED(gauging_status);
    return true;
}

uint16_t bq27220_get_temperature(const FuriHalI2cBusHandle* handle) {
                   UNUSED(handle);
    return 25;
}

uint16_t bq27220_get_full_charge_capacity(const FuriHalI2cBusHandle* handle) {
                       UNUSED(handle);
    return 100;
}

uint16_t bq27220_get_design_capacity(const FuriHalI2cBusHandle* handle) {
                      UNUSED(handle);
    return 100;
}

uint16_t bq27220_get_remaining_capacity(const FuriHalI2cBusHandle* handle) {
                      UNUSED(handle);
    return 100;
}

uint16_t bq27220_get_state_of_charge(const FuriHalI2cBusHandle* handle) {
                      UNUSED(handle);
    return 100;
}
uint16_t bq27220_get_state_of_health(const FuriHalI2cBusHandle* handle) {
                      UNUSED(handle);
    return 100;
}

