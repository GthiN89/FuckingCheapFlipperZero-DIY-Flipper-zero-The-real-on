#include <furi_hal_nfc_i.h>
#include <furi.h>
#include <lib/drivers/st25r3916.h> // Include for st25r3916 IRQ masks

#define TAG "FuriHalNfcEvent"

FuriHalNfcEventInternal* furi_hal_nfc_event = NULL;

void furi_hal_nfc_event_init(void) {
    FURI_LOG_D(TAG, "Initializing NFC event system");
    furi_hal_nfc_event = malloc(sizeof(FuriHalNfcEventInternal));
}

FuriHalNfcError furi_hal_nfc_event_start(void) {
    furi_check(furi_hal_nfc_event);

    furi_hal_nfc_event->thread = furi_thread_get_current_id();
    FURI_LOG_D(TAG, "Event system started for thread ID: %p", furi_hal_nfc_event->thread);
    furi_thread_flags_clear(FURI_HAL_NFC_EVENT_INTERNAL_ALL);

    return FuriHalNfcErrorNone;
}

FuriHalNfcError furi_hal_nfc_event_stop(void) {
    furi_check(furi_hal_nfc_event);
    FURI_LOG_D(TAG, "Event system stopped for thread ID: %p", furi_hal_nfc_event->thread);

    furi_hal_nfc_event->thread = NULL;

    return FuriHalNfcErrorNone;
}

void furi_hal_nfc_event_set(FuriHalNfcEventInternalType event) {
    furi_check(furi_hal_nfc_event);

    if(furi_hal_nfc_event->thread) {
        furi_thread_flags_set(furi_hal_nfc_event->thread, event);
    }
}

FuriHalNfcError furi_hal_nfc_abort(void) {
    FURI_LOG_D(TAG, "Abort requested");
    furi_hal_nfc_event_set(FuriHalNfcEventInternalTypeAbort);
    return FuriHalNfcErrorNone;
}

FuriHalNfcEvent furi_hal_nfc_wait_event_common(uint32_t timeout_ms) {
    furi_check(furi_hal_nfc_event);
    furi_check(furi_hal_nfc_event->thread);
    //FURI_LOG_T(TAG, "Waiting for event, timeout: %lu ms", timeout_ms);

    FuriHalNfcEvent event = 0;
    uint32_t event_timeout = timeout_ms == FURI_HAL_NFC_EVENT_WAIT_FOREVER ? FuriWaitForever :
                                                                             timeout_ms;
    uint32_t event_flag =
        furi_thread_flags_wait(FURI_HAL_NFC_EVENT_INTERNAL_ALL, FuriFlagWaitAny, event_timeout);
    if(event_flag != (unsigned)FuriFlagErrorTimeout) {
        //FURI_LOG_T(TAG, "Got event flag: 0x%08lX", event_flag);
        if(event_flag & FuriHalNfcEventInternalTypeIrq) {
            //FURI_LOG_T(TAG, "Processing IRQ event");
            furi_thread_flags_clear(FuriHalNfcEventInternalTypeIrq);
            const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
            uint32_t irq = furi_hal_nfc_get_irq(handle);

            //FURI_LOG_T(TAG, "NFC chip IRQ mask: 0x%08lX", irq);
            if(irq & ST25R3916_IRQ_MASK_OSC) {
                event |= FuriHalNfcEventOscOn;
            }
            if(irq & ST25R3916_IRQ_MASK_TXE) {
                event |= FuriHalNfcEventTxEnd;
            }
            if(irq & ST25R3916_IRQ_MASK_RXS) {
                event |= FuriHalNfcEventRxStart;
            }
            if(irq & ST25R3916_IRQ_MASK_RXE) {
                event |= FuriHalNfcEventRxEnd;
            }
            if(irq & ST25R3916_IRQ_MASK_COL) {
                event |= FuriHalNfcEventCollision;
            }
            if(irq & ST25R3916_IRQ_MASK_EON) {
                event |= FuriHalNfcEventFieldOn;
            }
            if(irq & ST25R3916_IRQ_MASK_EOF) {
                event |= FuriHalNfcEventFieldOff;
            }
            if(irq & ST25R3916_IRQ_MASK_WU_A) {
                event |= FuriHalNfcEventListenerActive;
            }
            if(irq & ST25R3916_IRQ_MASK_WU_A_X) {
                event |= FuriHalNfcEventListenerActive;
            }
            if(irq & ST25R3916_IRQ_MASK_WU_F) {
                event |= FuriHalNfcEventListenerActive;
            }
        }
        if(event_flag & FuriHalNfcEventInternalTypeTimerFwtExpired) {
            //FURI_LOG_T(TAG, "Processing FWT Timer Expired event");
            event |= FuriHalNfcEventTimerFwtExpired;
            furi_thread_flags_clear(FuriHalNfcEventInternalTypeTimerFwtExpired);
        }
        if(event_flag & FuriHalNfcEventInternalTypeTimerBlockTxExpired) {
            //FURI_LOG_T(TAG, "Processing Block TX Timer Expired event");
            event |= FuriHalNfcEventTimerBlockTxExpired;
            furi_thread_flags_clear(FuriHalNfcEventInternalTypeTimerBlockTxExpired);
        }
        if(event_flag & FuriHalNfcEventInternalTypeAbort) {
            FURI_LOG_D(TAG, "Processing Abort Request event");
            event |= FuriHalNfcEventAbortRequest;
            furi_thread_flags_clear(FuriHalNfcEventInternalTypeAbort);
        }
    } else {
        //FURI_LOG_T(TAG, "Event wait timeout");
        event = FuriHalNfcEventTimeout;
    }

    return event;
}

bool furi_hal_nfc_event_wait_for_specific_irq(
    const FuriHalSpiBusHandle* handle,
    uint32_t mask,
    uint32_t timeout_ms) {
    furi_check(furi_hal_nfc_event);
    furi_check(furi_hal_nfc_event->thread);
    //FURI_LOG_T(TAG, "Waiting for specific IRQ mask: 0x%08lX, timeout: %lu ms", mask, timeout_ms);

    bool irq_received = false;
    uint32_t event_flag =
        furi_thread_flags_wait(FuriHalNfcEventInternalTypeIrq, FuriFlagWaitAny, timeout_ms);
    if(event_flag == FuriHalNfcEventInternalTypeIrq) {
        uint32_t irq = furi_hal_nfc_get_irq(handle);

        //FURI_LOG_T(TAG, "IRQ event received, chip IRQ mask: 0x%08lX", irq);
        irq_received = ((irq & mask) == mask);
        furi_thread_flags_clear(FuriHalNfcEventInternalTypeIrq);
    } else {
        //FURI_LOG_T(TAG, "Wait for specific IRQ timed out or failed");
    }

    //FURI_LOG_T(TAG, "Returning IRQ received: %s", irq_received ? "true" : "false");
    return irq_received;
}
