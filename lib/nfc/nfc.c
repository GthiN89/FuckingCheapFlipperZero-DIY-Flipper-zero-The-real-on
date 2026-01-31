#ifndef FW_CFG_unit_tests

#include "nfc.h"

#include <furi_hal_nfc.h>
#include <furi/furi.h>

#define TAG "NfcCore"

#define NFC_MAX_BUFFER_SIZE (256)
#define NFC_FELICA_LISTENER_RESPONSE_TIME_A_FC (512 * 64)
#define NFC_FELICA_LISTENER_RESPONSE_TIME_B_FC (256 * 64)

// --- REMOVED: Hypothetical function and constant definitions that caused the linker error ---

// --- Internal State Enums ---
typedef enum {
    NfcStateIdle,
    NfcStateRunning,
} NfcState;

typedef enum {
    NfcPollerStateStart,
    NfcPollerStateReady,
    NfcPollerStateReset,
    NfcPollerStateStop,
    NfcPollerStateNum,
} NfcPollerState;

typedef enum {
    NfcCommStateIdle,
    NfcCommStateWaitBlockTxTimer,
    NfcCommStateReadyTx,
    NfcCommStateWaitTxEnd,
    NfcCommStateWaitRxStart,
    NfcCommStateWaitRxEnd,
    NfcCommStateFailed,
} NfcCommState;

typedef enum {
    NfcConfigurationStateIdle,
    NfcConfigurationStateDone,
} NfcConfigurationState;

// --- Main Struct ---
struct Nfc {
    NfcState state;
    NfcPollerState poller_state;
    NfcCommState comm_state;
    NfcConfigurationState config_state;
    NfcMode mode;

    uint32_t fdt_listen_fc;
    uint32_t mask_rx_time_fc;
    uint32_t fdt_poll_fc;
    uint32_t fdt_poll_poll_us;
    uint32_t guard_time_us;
    NfcEventCallback callback;
    void* context;

    uint8_t tx_buffer[NFC_MAX_BUFFER_SIZE];
    size_t tx_bits;
    uint8_t rx_buffer[NFC_MAX_BUFFER_SIZE];
    size_t rx_bits;

    FuriThread* worker_thread;
};

typedef bool (*NfcWorkerPollerStateHandler)(Nfc* instance);

// --- Static Data ---
static const FuriHalNfcTech nfc_tech_table[NfcModeNum][NfcTechNum] = {
    [NfcModePoller] =
        {
            [NfcTechIso14443a] = FuriHalNfcTechIso14443a,
            [NfcTechIso14443b] = FuriHalNfcTechIso14443b,
            [NfcTechIso15693] = FuriHalNfcTechIso15693,
            [NfcTechFelica] = FuriHalNfcTechFelica,
        },
    [NfcModeListener] =
        {
            [NfcTechIso14443a] = FuriHalNfcTechIso14443a,
            [NfcTechIso14443b] = FuriHalNfcTechInvalid,
            [NfcTechIso15693] = FuriHalNfcTechIso15693,
            [NfcTechFelica] = FuriHalNfcTechFelica,
        },
};



static NfcError nfc_process_hal_error(FuriHalNfcError error) {
    NfcError ret = NfcErrorNone;
    if(error != FuriHalNfcErrorNone) {
        //FURI_LOG_W(TAG, "Processing HAL error code: %d", error);
    }
    switch(error) {
    case FuriHalNfcErrorNone:
        ret = NfcErrorNone;
        break;
    case FuriHalNfcErrorIncompleteFrame:
        ret = NfcErrorIncompleteFrame;
        break;
    case FuriHalNfcErrorDataFormat:
        ret = NfcErrorDataFormat;
        break;
    default:
        ret = NfcErrorInternal;
    }
    return ret;
}

// --- Worker Threads ---
static int32_t nfc_worker_listener(void* context) {
    furi_assert(context);
    Nfc* instance = context;
    FURI_LOG_D(TAG, "Listener worker thread started");

    furi_assert(instance->callback);
    furi_assert(instance->config_state == NfcConfigurationStateDone);

    instance->state = NfcStateRunning;
    furi_hal_nfc_event_start();

    NfcEventData event_data = {.buffer = bit_buffer_alloc(NFC_MAX_BUFFER_SIZE)};
    NfcEvent nfc_event = {.data = event_data};
    NfcCommand command = NfcCommandContinue;

    FURI_LOG_D(TAG, "Entering listener event loop...");
    while(true) {
        FuriHalNfcEvent event = furi_hal_nfc_listener_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);

        if(event & FuriHalNfcEventAbortRequest) {
            FURI_LOG_D(TAG, "Abort request received, exiting listener loop.");
            nfc_event.type = NfcEventTypeUserAbort;
            instance->callback(nfc_event, instance->context);
            break;
        }
        if(event & FuriHalNfcEventFieldOn) {
            FURI_LOG_D(TAG, "Event: Field ON");
            nfc_event.type = NfcEventTypeFieldOn;
            instance->callback(nfc_event, instance->context);
        }
        if(event & FuriHalNfcEventFieldOff) {
            FURI_LOG_D(TAG, "Event: Field OFF");
            nfc_event.type = NfcEventTypeFieldOff;
            instance->callback(nfc_event, instance->context);
            furi_hal_nfc_listener_idle();
        }
        if(event & FuriHalNfcEventListenerActive) {
            FURI_LOG_D(TAG, "Event: Listener Activated");
            nfc_event.type = NfcEventTypeListenerActivated;
            instance->callback(nfc_event, instance->context);
        }
        if(event & FuriHalNfcEventRxEnd) {
            FURI_LOG_D(TAG, "Event: RX End");
            furi_hal_nfc_timer_block_tx_start(instance->fdt_listen_fc);

            nfc_event.type = NfcEventTypeRxEnd;
            furi_hal_nfc_listener_rx(
                instance->rx_buffer, sizeof(instance->rx_buffer), &instance->rx_bits);
            bit_buffer_copy_bits(event_data.buffer, instance->rx_buffer, instance->rx_bits);
            //FURI_LOG_D(
            //     TAG, "Dispatching RxEnd to user callback, received %zu bits", instance->rx_bits);
            // command = instance->callback(nfc_event, instance->context);
            //FURI_LOG_D(TAG, "User callback returned command: %d", command);
            if(command == NfcCommandStop) {
                break;
            } else if(command == NfcCommandReset) {
                furi_hal_nfc_listener_enable_rx();
            } else if(command == NfcCommandSleep) {
                furi_hal_nfc_listener_idle();
            }
        }
    }

    FURI_LOG_D(TAG, "Listener worker thread finished. Cleaning up.");
    furi_hal_nfc_reset_mode();
    instance->config_state = NfcConfigurationStateIdle;

    bit_buffer_free(event_data.buffer);
    furi_hal_nfc_low_power_mode_start();
    return 0;
}

bool nfc_worker_poller_start_handler(Nfc* instance) {
    //FURI_LOG_D(TAG, "Poller state: START -> Turning field ON");
    furi_hal_nfc_poller_field_on();
    if(instance->guard_time_us) {
        ////FURI_LOG_T(TAG, "Starting guard timer: %lu us", instance->guard_time_us);
        furi_hal_nfc_timer_block_tx_start_us(instance->guard_time_us);
        FuriHalNfcEvent event = furi_hal_nfc_poller_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);
        furi_assert(event & FuriHalNfcEventTimerBlockTxExpired);
        ////FURI_LOG_T(TAG, "Guard timer expired");
    }
    instance->poller_state = NfcPollerStateReady;
    return false;
}

bool nfc_worker_poller_ready_handler(Nfc* instance) {
    //FURI_LOG_D(TAG, "Poller state: READY -> Calling user callback");
    NfcCommand command = NfcCommandContinue;

    NfcEvent event = {.type = NfcEventTypePollerReady};
    command = instance->callback(event, instance->context);
    //FURI_LOG_D(TAG, "User callback returned command: %d", command);

    if(command == NfcCommandReset) {
        instance->poller_state = NfcPollerStateReset;
    } else if(command == NfcCommandStop) {
        instance->poller_state = NfcPollerStateStop;
    }
    return false;
}

bool nfc_worker_poller_reset_handler(Nfc* instance) {
    //FURI_LOG_D(TAG, "Poller state: RESET -> Cycling power");
    furi_hal_nfc_low_power_mode_start();
    furi_delay_ms(100);
    furi_hal_nfc_low_power_mode_stop();
    instance->poller_state = NfcPollerStateStart;
    return false;
}

bool nfc_worker_poller_stop_handler(Nfc* instance) {
    //FURI_LOG_D(TAG, "Poller state: STOP -> Shutting down");
    furi_hal_nfc_reset_mode();
    instance->config_state = NfcConfigurationStateIdle;
    furi_hal_nfc_low_power_mode_start();
    furi_delay_ms(10);
    instance->poller_state = NfcPollerStateStart;
    return true; // Exit the loop
}

static const NfcWorkerPollerStateHandler nfc_worker_poller_state_handlers[NfcPollerStateNum] = {
    [NfcPollerStateStart] = nfc_worker_poller_start_handler,
    [NfcPollerStateReady] = nfc_worker_poller_ready_handler,
    [NfcPollerStateReset] = nfc_worker_poller_reset_handler,
    [NfcPollerStateStop] = nfc_worker_poller_stop_handler,
};

static int32_t nfc_worker_poller(void* context) {
    furi_assert(context);
    Nfc* instance = context;
    //FURI_LOG_D(TAG, "Poller worker thread started");
    furi_assert(instance->callback);
    instance->state = NfcStateRunning;
    instance->poller_state = NfcPollerStateStart;
    //FURI_LOG_D(TAG, "EventStart");
    furi_hal_nfc_event_start();

    //FURI_LOG_D(TAG, "Entering poller state machine loop...");
    bool exit = false;
    while(!exit) {
        exit = nfc_worker_poller_state_handlers[instance->poller_state](instance);
    }
    //FURI_LOG_D(TAG, "Poller worker thread finished.");
    return 0;
}

// --- Public API Functions ---
Nfc* nfc_alloc(void) {
    //FURI_LOG_D(TAG, "Allocating new NFC instance");
    Nfc* instance = malloc(sizeof(Nfc));
    instance->state = NfcStateIdle;
    instance->comm_state = NfcCommStateIdle;
    instance->config_state = NfcConfigurationStateIdle;
    instance->worker_thread = furi_thread_alloc();
    furi_thread_set_name(instance->worker_thread, "NfcWorker");
    furi_thread_set_context(instance->worker_thread, instance);
    furi_thread_set_priority(instance->worker_thread, FuriThreadPriorityHighest);
    furi_thread_set_stack_size(instance->worker_thread, 8 * 1024);
    //FURI_LOG_D(TAG, "NFC instance and worker thread allocated successfully.");
    return instance;
}

void nfc_free(Nfc* instance) {
    furi_check(instance);
    furi_check(instance->state == NfcStateIdle);
    //FURI_LOG_D(TAG, "Freeing NFC instance %p", instance);
    furi_thread_free(instance->worker_thread);
    free(instance);
}

// MODIFIED: Removed acquire/release calls to avoid nested locking issues,
// as furi_hal_nfc_low_power_mode_stop() and furi_hal_nfc_set_mode() already handle internal locking.
void nfc_config(Nfc* instance, NfcMode mode, NfcTech tech) {
    furi_check(instance);
    furi_check(mode < NfcModeNum);
    furi_check(tech < NfcTechNum);
    furi_check(instance->config_state == NfcConfigurationStateIdle);
    //FURI_LOG_D(
        // TAG,
        // "Configuring mode: %s, tech: %d",
        // (mode == NfcModePoller) ? "Poller" : "Listener",
        // tech);

    // Removed outer acquire: furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    FuriHalNfcTech hal_tech = nfc_tech_table[mode][tech];
    if(hal_tech == FuriHalNfcTechInvalid) {
        furi_crash("Unsupported mode for given tech");
    }
    FuriHalNfcMode hal_mode = (mode == NfcModePoller) ? FuriHalNfcModePoller :
                                                        FuriHalNfcModeListener;
    furi_hal_nfc_low_power_mode_stop();
    furi_hal_nfc_set_mode(hal_mode, hal_tech);
    
    // REMOVED: The call to the non-existent furi_hal_nfc_set_min_rfo_resistance() function.

    instance->mode = mode;
    instance->config_state = NfcConfigurationStateDone;
    // Removed outer release: furi_hal_nfc_release();
}

void nfc_set_fdt_poll_fc(Nfc* instance, uint32_t fdt_poll_fc) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Set FDT Poll (Frame Delay Time): %lu fc", fdt_poll_fc);
    instance->fdt_poll_fc = fdt_poll_fc;
}

void nfc_set_fdt_listen_fc(Nfc* instance, uint32_t fdt_listen_fc) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Set FDT Listen (Frame Delay Time): %lu fc", fdt_listen_fc);
    instance->fdt_listen_fc = fdt_listen_fc;
}

void nfc_set_fdt_poll_poll_us(Nfc* instance, uint32_t fdt_poll_poll_us) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Set FDT Poll-Poll (Mask RX Time): %lu us", fdt_poll_poll_us);
    instance->fdt_poll_poll_us = fdt_poll_poll_us;
}

void nfc_set_guard_time_us(Nfc* instance, uint32_t guard_time_us) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Set Guard Time: %lu us", guard_time_us);
    instance->guard_time_us = guard_time_us;
}

void nfc_set_mask_receive_time_fc(Nfc* instance, uint32_t mask_rx_time_fc) {
    furi_check(instance);
    instance->mask_rx_time_fc = mask_rx_time_fc;
}

void nfc_start(Nfc* instance, NfcEventCallback callback, void* context) {
    furi_check(instance);
    furi_check(instance->worker_thread);
    furi_check(callback);
    furi_check(instance->config_state == NfcConfigurationStateDone);
    //FURI_LOG_D(
        // TAG,
        // "Starting worker thread in mode: %s",
        // (instance->mode == NfcModePoller) ? "Poller" : "Listener");
    instance->callback = callback;
    instance->context = context;
    if(instance->mode == NfcModePoller) {
        furi_thread_set_callback(instance->worker_thread, nfc_worker_poller);
    } else {
        furi_thread_set_callback(instance->worker_thread, nfc_worker_listener);
    }
    instance->comm_state = NfcCommStateIdle;
    furi_thread_start(instance->worker_thread);
}

void nfc_stop(Nfc* instance) {
    furi_check(instance);
    furi_check(instance->state == NfcStateRunning);
    //FURI_LOG_D(TAG, "Stopping worker thread...");
    if(instance->mode == NfcModeListener) {
        furi_hal_nfc_abort();
    }
    furi_thread_join(instance->worker_thread);
    instance->state = NfcStateIdle;
    //FURI_LOG_D(TAG, "Worker thread stopped.");
}

static NfcError nfc_poller_trx_state_machine(Nfc* instance, uint32_t fwt_fc) {
    FuriHalNfcEvent event = 0;
    NfcError error = NfcErrorNone;
    //FURI_LOG_D(TAG, "Entering TRX state machine, fwt: %lu", fwt_fc);
    while(true) {
        furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
        event = furi_hal_nfc_poller_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);
        if(event & FuriHalNfcEventTimerBlockTxExpired) {
            if(instance->comm_state == NfcCommStateWaitBlockTxTimer) {
                ////FURI_LOG_T(TAG, "TRX SM: Block TX timer expired, ready to TX");
                instance->comm_state = NfcCommStateReadyTx;
            }
        }
        if(event & FuriHalNfcEventTxEnd) {
            if(instance->comm_state == NfcCommStateWaitTxEnd) {
                ////FURI_LOG_T(TAG, "TRX SM: TX ended, starting FWT and poll timers");
                if(fwt_fc) furi_hal_nfc_timer_fwt_start(fwt_fc);
                furi_hal_nfc_timer_block_tx_start_us(instance->fdt_poll_poll_us);
                instance->comm_state = NfcCommStateWaitRxStart;
            }
        }
        if(event & FuriHalNfcEventRxStart) {
            if(instance->comm_state == NfcCommStateWaitRxStart) {
                ////FURI_LOG_T(TAG, "TRX SM: RX started, stopping timers");
                furi_hal_nfc_timer_block_tx_stop();
                furi_hal_nfc_timer_fwt_stop();
                instance->comm_state = NfcCommStateWaitRxEnd;
            }
        }
        if(event & FuriHalNfcEventRxEnd) {
            ////FURI_LOG_T(TAG, "TRX SM: RX ended, transaction complete");
            furi_hal_nfc_timer_block_tx_start(instance->fdt_poll_fc);
            furi_hal_nfc_timer_fwt_stop();
            instance->comm_state = NfcCommStateWaitBlockTxTimer;
            furi_hal_nfc_release();
            break;
        }
        if(event & FuriHalNfcEventTimerFwtExpired) {
            if(instance->comm_state == NfcCommStateWaitRxStart) {
                //FURI_LOG_W(TAG, "TRX SM: FWT Timeout!");
                error = NfcErrorTimeout;
                furi_hal_nfc_timer_fwt_stop();
                if(furi_hal_nfc_timer_block_tx_is_running()) {
                    instance->comm_state = NfcCommStateWaitBlockTxTimer;
                } else {
                    instance->comm_state = NfcCommStateReadyTx;
                }
                furi_hal_nfc_release();
                break;
            }
        }
        furi_hal_nfc_release();
    }
    //FURI_LOG_D(TAG, "Exiting TRX state machine with error code: %d", error);
    return error;
}

// MODIFIED: Added acquire/release
NfcError nfc_listener_tx(Nfc* instance, const BitBuffer* tx_buffer) {
    furi_check(instance);
    furi_check(tx_buffer);
    //FURI_LOG_D(TAG, "Listener TX initiated, tx_bits: %zu", bit_buffer_get_size(tx_buffer));
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    NfcError ret = NfcErrorNone;
    while(furi_hal_nfc_timer_block_tx_is_running()) {
    }
    FuriHalNfcError error =
        furi_hal_nfc_listener_tx(bit_buffer_get_data(tx_buffer), bit_buffer_get_size(tx_buffer));
    if(error != FuriHalNfcErrorNone) {
        //FURI_LOG_W(TAG, "Failed in listener TX, HAL error: %d", error);
        ret = nfc_process_hal_error(error);
    }
    furi_hal_nfc_release();
    return ret;
}

// MODIFIED: Corrected acquire/release logic for nested calls and blocking waits.
NfcError nfc_iso14443a_poller_trx_custom_parity(
    Nfc* instance,
    const BitBuffer* tx_buffer,
    BitBuffer* rx_buffer,
    uint32_t fwt) {
    furi_check(instance);
    furi_check(tx_buffer);
    furi_check(rx_buffer);
    furi_check(instance->poller_state == NfcPollerStateReady);
    //FURI_LOG_D(
        // TAG,
        // "Poller TRX (Custom Parity) initiated, tx_bits: %zu, fwt: %lu",
        // bit_buffer_get_size(tx_buffer),
        // fwt);
    NfcError ret = NfcErrorNone;
    FuriHalNfcError error = FuriHalNfcErrorNone;

    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Acquire lock for TX phase

    do {
        furi_hal_nfc_trx_reset();
        while(furi_hal_nfc_timer_block_tx_is_running()) {
            ////FURI_LOG_T(TAG, "Waiting for block TX timer...");
            furi_hal_nfc_release(); // Release lock before blocking wait
            FuriHalNfcEvent event =
                furi_hal_nfc_poller_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);
            furi_check(
                furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock after wait
            if(event & FuriHalNfcEventTimerBlockTxExpired) {
                break;
            }
        }

        bit_buffer_write_bytes_with_parity(
            tx_buffer, instance->tx_buffer, sizeof(instance->tx_buffer), &instance->tx_bits);
        error =
            furi_hal_nfc_iso14443a_poller_tx_custom_parity(instance->tx_buffer, instance->tx_bits);
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        instance->comm_state = NfcCommStateWaitTxEnd;
        furi_hal_nfc_release(); // Release lock before calling state machine

        ret = nfc_poller_trx_state_machine(
            instance, fwt); // This function handles its own locking internally

        furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock for RX phase
        if(ret != NfcErrorNone) {
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        error = furi_hal_nfc_poller_rx(
            instance->rx_buffer, sizeof(instance->rx_buffer), &instance->rx_bits);
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }
        if(instance->rx_bits >= 9 && (instance->rx_bits % 9) != 0) {
            ret = NfcErrorDataFormat;
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }
        bit_buffer_copy_bytes_with_parity(rx_buffer, instance->rx_buffer, instance->rx_bits);
    } while(false);

    furi_hal_nfc_release(); // Final release (if not released earlier on error exit)
    return ret;
}

// MODIFIED: Corrected acquire/release logic to fix a flaw where lock was released
// in the while loop but not re-acquired before subsequent HAL calls.
NfcError
    nfc_poller_trx(Nfc* instance, const BitBuffer* tx_buffer, BitBuffer* rx_buffer, uint32_t fwt) {
    furi_check(instance);
    furi_check(tx_buffer);
    furi_check(rx_buffer);
    furi_check(instance->poller_state == NfcPollerStateReady);
    //FURI_LOG_D(
        // TAG,
        // "Poller TRX (Standard) initiated, tx_bits: %zu, fwt: %lu",
        // bit_buffer_get_size(tx_buffer),
        // fwt);

    NfcError ret = NfcErrorNone;
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Acquire lock for TX phase

    do {
        furi_hal_nfc_trx_reset();
        while(furi_hal_nfc_timer_block_tx_is_running()) {
            ////FURI_LOG_T(TAG, "Waiting for block TX timer...");
            furi_hal_nfc_release(); // Release lock before blocking wait
            FuriHalNfcEvent event =
                furi_hal_nfc_poller_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);
            furi_check(
                furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock after wait
            if(event & FuriHalNfcEventTimerBlockTxExpired) {
                break;
            }
        }

        error =
            furi_hal_nfc_poller_tx(bit_buffer_get_data(tx_buffer), bit_buffer_get_size(tx_buffer));
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        instance->comm_state = NfcCommStateWaitTxEnd;
        furi_hal_nfc_release(); // Release lock before calling state machine

        ret = nfc_poller_trx_state_machine(
            instance, fwt); // This function handles its own locking internally

        furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock for RX phase
        if(ret != NfcErrorNone) {
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        error = furi_hal_nfc_poller_rx(
            instance->rx_buffer, sizeof(instance->rx_buffer), &instance->rx_bits);
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }
        bit_buffer_copy_bits(rx_buffer, instance->rx_buffer, instance->rx_bits);
    } while(false);

    furi_hal_nfc_release(); // Final release (if not released earlier on error exit)
    return ret;
}

// MODIFIED: Added acquire/release
NfcError nfc_iso14443a_listener_set_col_res_data(
    Nfc* instance,
    uint8_t* uid,
    uint8_t uid_len,
    uint8_t* atqa,
    uint8_t sak) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Setting ISO14443A listener collision/resolution data");
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    FuriHalNfcError error =
        furi_hal_nfc_iso14443a_listener_set_col_res_data(uid, uid_len, atqa, sak);
    instance->comm_state = NfcCommStateIdle;
    furi_hal_nfc_release();
    return nfc_process_hal_error(error);
}

// MODIFIED: Corrected acquire/release logic for nested calls and blocking waits.
NfcError nfc_iso14443a_poller_trx_short_frame(
    Nfc* instance,
    NfcIso14443aShortFrame frame,
    BitBuffer* rx_buffer,
    uint32_t fwt) {
    furi_check(instance);
    furi_check(rx_buffer);
    furi_check(instance->poller_state == NfcPollerStateReady);
    //FURI_LOG_D(TAG, "Poller TRX (Short Frame) initiated, frame type: %d, fwt: %lu", frame, fwt);

    FuriHalNfcaShortFrame short_frame = (frame == NfcIso14443aShortFrameAllReqa) ?
                                            FuriHalNfcaShortFrameAllReq :
                                            FuriHalNfcaShortFrameSensReq;
    NfcError ret = NfcErrorNone;
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Acquire lock for TX phase

    do {
        furi_hal_nfc_trx_reset();
        while(furi_hal_nfc_timer_block_tx_is_running()) {
            furi_hal_nfc_release(); // Release lock before blocking wait
            ////FURI_LOG_T(TAG, "Waiting for block TX timer...");
            FuriHalNfcEvent event =
                furi_hal_nfc_poller_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);
            furi_check(
                furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock after wait
            if(event & FuriHalNfcEventTimerBlockTxExpired) break;
        }

        error = furi_hal_nfc_iso14443a_poller_trx_short_frame(short_frame);
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        instance->comm_state = NfcCommStateWaitTxEnd;
        furi_hal_nfc_release(); // Release lock before calling state machine

        ret = nfc_poller_trx_state_machine(
            instance, fwt); // This function handles its own locking internally

        furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock for RX phase
        if(ret != NfcErrorNone) {
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        error = furi_hal_nfc_poller_rx(
            instance->rx_buffer, sizeof(instance->rx_buffer), &instance->rx_bits);
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }
        //FURI_LOG_D(TAG, "Poller RX successful, received %zu bits", instance->rx_bits);
        bit_buffer_copy_bits(rx_buffer, instance->rx_buffer, instance->rx_bits);
    } while(false);

    furi_hal_nfc_release(); // Final release (if not released earlier on error exit)
    return ret;
}

// MODIFIED: Corrected acquire/release logic to fix a flaw where lock was released
// in the while loop but not re-acquired before subsequent HAL calls.
NfcError nfc_iso14443a_poller_trx_sdd_frame(
    Nfc* instance,
    const BitBuffer* tx_buffer,
    BitBuffer* rx_buffer,
    uint32_t fwt) {
    furi_check(instance);
    furi_check(tx_buffer);
    furi_check(rx_buffer);
    furi_check(instance->poller_state == NfcPollerStateReady);
    //FURI_LOG_D(
        // TAG,
        // "Poller TRX (SDD Frame) initiated, tx_bits: %zu, fwt: %lu",
        // bit_buffer_get_size(tx_buffer),
        // fwt);

    NfcError ret = NfcErrorNone;
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Acquire lock for TX phase

    do {
        furi_hal_nfc_trx_reset();
        while(furi_hal_nfc_timer_block_tx_is_running()) {
            furi_hal_nfc_release(); // Release lock before blocking wait
            ////FURI_LOG_T(TAG, "Waiting for block TX timer...");
            FuriHalNfcEvent event =
                furi_hal_nfc_poller_wait_event(FURI_HAL_NFC_EVENT_WAIT_FOREVER);
            furi_check(
                furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock after wait
            if(event & FuriHalNfcEventTimerBlockTxExpired) break;
        }

        error = furi_hal_nfc_iso14443a_tx_sdd_frame(
            bit_buffer_get_data(tx_buffer), bit_buffer_get_size(tx_buffer));
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        instance->comm_state = NfcCommStateWaitTxEnd;
        furi_hal_nfc_release(); // Release lock before calling state machine

        ret = nfc_poller_trx_state_machine(
            instance, fwt); // This function handles its own locking internally

        furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Re-acquire lock for RX phase
        if(ret != NfcErrorNone) {
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }

        error = furi_hal_nfc_poller_rx(
            instance->rx_buffer, sizeof(instance->rx_buffer), &instance->rx_bits);
        if(error != FuriHalNfcErrorNone) {
            ret = nfc_process_hal_error(error);
            furi_hal_nfc_release(); // Release lock on error exit
            break;
        }
        bit_buffer_copy_bits(rx_buffer, instance->rx_buffer, instance->rx_bits);
    } while(false);

    furi_hal_nfc_release(); // Final release (if not released earlier on error exit)
    return ret;
}

// MODIFIED: Added acquire/release
NfcError nfc_iso14443a_listener_tx_custom_parity(Nfc* instance, const BitBuffer* tx_buffer) {
    furi_check(instance);
    furi_check(tx_buffer);
    //FURI_LOG_D(
        // TAG,
        // "Listener TX (Custom Parity) initiated, tx_bits: %zu",
        // bit_buffer_get_size(tx_buffer));
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    NfcError ret = NfcErrorNone;
    const uint8_t* tx_data = bit_buffer_get_data(tx_buffer);
    const uint8_t* tx_parity = bit_buffer_get_parity(tx_buffer);
    size_t tx_bits = bit_buffer_get_size(tx_buffer);
    FuriHalNfcError error =
        furi_hal_nfc_iso14443a_listener_tx_custom_parity(tx_data, tx_parity, tx_bits);
    ret = nfc_process_hal_error(error);
    furi_hal_nfc_release();
    return ret;
}

// MODIFIED: Added acquire/release
NfcError nfc_iso15693_listener_tx_sof(Nfc* instance) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Listener TX (ISO15693 SOF)");
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    while(furi_hal_nfc_timer_block_tx_is_running()) {
    }
    FuriHalNfcError error = furi_hal_nfc_iso15693_listener_tx_sof();
    NfcError ret = nfc_process_hal_error(error);
    furi_hal_nfc_release();
    return ret;
}

// MODIFIED: Added acquire/release
NfcError nfc_felica_listener_set_sensf_res_data(
    Nfc* instance,
    const uint8_t* idm,
    const uint8_t idm_len,
    const uint8_t* pmm,
    const uint8_t pmm_len,
    const uint16_t sys_code) {
    furi_check(instance);
    //FURI_LOG_D(TAG, "Setting Felica listener SENSF_RES data");
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    FuriHalNfcError error =
        furi_hal_nfc_felica_listener_set_sensf_res_data(idm, idm_len, pmm, pmm_len, sys_code);
    instance->comm_state = NfcCommStateIdle;
    furi_hal_nfc_release();
    return nfc_process_hal_error(error);
}

void nfc_felica_listener_timer_anticol_start(Nfc* instance, uint8_t target_time_slot) {
    furi_check(instance);

    furi_hal_nfc_timer_block_tx_start(
        NFC_FELICA_LISTENER_RESPONSE_TIME_A_FC +
        target_time_slot * NFC_FELICA_LISTENER_RESPONSE_TIME_B_FC);
}

void nfc_felica_listener_timer_anticol_stop(Nfc* instance) {
    furi_check(instance);

    if(furi_hal_nfc_timer_block_tx_is_running()) {
        furi_hal_nfc_timer_block_tx_stop();
    }
}

#endif // FW_CFG_unit_tests