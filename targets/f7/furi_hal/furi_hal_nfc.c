#include "furi_hal_nfc_i.h"
#include "furi_hal_nfc_tech_i.h"

#include <lib/drivers/st25r3916.h>

#include <furi.h>
#include <furi_hal_spi.h>

#define TAG "FuriHalNfc"

const FuriHalNfcTechBase* const furi_hal_nfc_tech[FuriHalNfcTechNum] = {
    [FuriHalNfcTechIso14443a] = &furi_hal_nfc_iso14443a,
    [FuriHalNfcTechIso14443b] = &furi_hal_nfc_iso14443b,
    [FuriHalNfcTechIso15693] = &furi_hal_nfc_iso15693,
    [FuriHalNfcTechFelica] = &furi_hal_nfc_felica,
    // Add new technologies here
};

FuriHalNfc furi_hal_nfc;

// void //furi_hal_nfc_log_irq(const char* action, uint32_t irq_mask) {
//     if(irq_mask == ST25R3916_IRQ_MASK_NONE) {
//         FURI_LOG_D(TAG, "%s: ST25R3916_IRQ_MASK_NONE (0x00000000)", action);
//         return;
//     }
//     if(irq_mask == ST25R3916_IRQ_MASK_ALL) {
//         FURI_LOG_D(TAG, "%s: ST25R3916_IRQ_MASK_ALL (0xFFFFFFFF)", action);
//         return;
//     }

//     FURI_LOG_D(TAG, "%s with mask 0x%08lX:", action, irq_mask);
//     if(irq_mask & ST25R3916_IRQ_MASK_OSC) FURI_LOG_D(TAG, "  - OSC: Oscillator stable");
//     if(irq_mask & ST25R3916_IRQ_MASK_FWL) FURI_LOG_D(TAG, "  - FWL: FIFO water level");
//     if(irq_mask & ST25R3916_IRQ_MASK_RXS) FURI_LOG_D(TAG, "  - RXS: Start of receive");
//     if(irq_mask & ST25R3916_IRQ_MASK_RXE) FURI_LOG_D(TAG, "  - RXE: End of receive");
//     if(irq_mask & ST25R3916_IRQ_MASK_TXE) FURI_LOG_D(TAG, "  - TXE: End of transmission");
//     if(irq_mask & ST25R3916_IRQ_MASK_COL) FURI_LOG_D(TAG, "  - COL: Bit collision");
//     if(irq_mask & ST25R3916_IRQ_MASK_RX_REST)
//         FURI_LOG_D(TAG, "  - RX_REST: Automatic reception restart");
//     if(irq_mask & ST25R3916_IRQ_MASK_DCT)
//         FURI_LOG_D(TAG, "  - DCT: Termination of direct command");
//     if(irq_mask & ST25R3916_IRQ_MASK_NRE) FURI_LOG_D(TAG, "  - NRE: No-response timer expired");
//     if(irq_mask & ST25R3916_IRQ_MASK_GPE)
//         FURI_LOG_D(TAG, "  - GPE: General purpose timer expired");
//     if(irq_mask & ST25R3916_IRQ_MASK_EON) FURI_LOG_D(TAG, "  - EON: External field on");
//     if(irq_mask & ST25R3916_IRQ_MASK_EOF) FURI_LOG_D(TAG, "  - EOF: External field off");
//     if(irq_mask & ST25R3916_IRQ_MASK_CAC)
//         FURI_LOG_D(TAG, "  - CAC: Collision during RF collision avoidance");
//     if(irq_mask & ST25R3916_IRQ_MASK_CAT) FURI_LOG_D(TAG, "  - CAT: Minimum guard time expired");
//     if(irq_mask & ST25R3916_IRQ_MASK_NFCT)
//         FURI_LOG_D(TAG, "  - NFCT: Initiator bit rate recognised");
//     if(irq_mask & ST25R3916_IRQ_MASK_CRC) FURI_LOG_D(TAG, "  - CRC: CRC error");
//     if(irq_mask & ST25R3916_IRQ_MASK_PAR) FURI_LOG_D(TAG, "  - PAR: Parity error");
//     if(irq_mask & ST25R3916_IRQ_MASK_ERR2) FURI_LOG_D(TAG, "  - ERR2: Soft framing error");
//     if(irq_mask & ST25R3916_IRQ_MASK_ERR1) FURI_LOG_D(TAG, "  - ERR1: Hard framing error");
//     if(irq_mask & ST25R3916_IRQ_MASK_WT) FURI_LOG_D(TAG, "  - WT: Wake-up interrupt");
//     if(irq_mask & ST25R3916_IRQ_MASK_WAM) FURI_LOG_D(TAG, "  - WAM: Wake-up due to amplitude");
//     if(irq_mask & ST25R3916_IRQ_MASK_WPH) FURI_LOG_D(TAG, "  - WPH: Wake-up due to phase");
//     if(irq_mask & ST25R3916_IRQ_MASK_WCAP)
//         FURI_LOG_D(TAG, "  - WCAP: Wake-up due to capacitance measurement");
//     if(irq_mask & ST25R3916_IRQ_MASK_PPON2) FURI_LOG_D(TAG, "  - PPON2: Field on waiting Timer");
//     if(irq_mask & ST25R3916_IRQ_MASK_SL_WL)
//         FURI_LOG_D(TAG, "  - SL_WL: Passive target slot number water level");
//     if(irq_mask & ST25R3916_IRQ_MASK_APON)
//         FURI_LOG_D(TAG, "  - APON: Anticollision done and Field On");
//     if(irq_mask & ST25R3916_IRQ_MASK_RXE_PTA)
//         FURI_LOG_D(TAG, "  - RXE_PTA: RXE with an automatic response");
//     if(irq_mask & ST25R3916_IRQ_MASK_WU_F)
//         FURI_LOG_D(TAG, "  - WU_F: 212/424b/s Passive target Active");
//     if(irq_mask & ST25R3916_IRQ_MASK_WU_A_X)
//         FURI_LOG_D(TAG, "  - WU_A_X: 106kb/s Passive target state Active*");
//     if(irq_mask & ST25R3916_IRQ_MASK_WU_A)
//         FURI_LOG_D(TAG, "  - WU_A: 106kb/s Passive target state Active");
// }

static FuriHalNfcError furi_hal_nfc_turn_on_osc(const FuriHalSpiBusHandle* handle) {
    FURI_LOG_D(TAG, "Turning on oscillator");
    FuriHalNfcError error = FuriHalNfcErrorNone;

    furi_hal_nfc_event_start();
    //  furi_hal_nfc_acquire(); // Removed nested acquire based on previous analysis
    if(!st25r3916_check_reg(
           handle,
           ST25R3916_REG_OP_CONTROL,
           ST25R3916_REG_OP_CONTROL_en,
           ST25R3916_REG_OP_CONTROL_en)) {
        FURI_LOG_D(TAG, "Oscillator not enabled, enabling it now");
        uint32_t irq_to_enable = ST25R3916_IRQ_MASK_OSC;
        //furi_hal_nfc_log_irq("Enabling IRQ", irq_to_enable);
        st25r3916_mask_irq(handle, ~irq_to_enable);
        st25r3916_set_reg_bits(handle, ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_en);
        //furi_hal_nfc_log_irq("Waiting for specific IRQ", ST25R3916_IRQ_MASK_OSC);
        if(furi_hal_nfc_event_wait_for_specific_irq(handle, ST25R3916_IRQ_MASK_OSC, 10)) {
            FURI_LOG_D(TAG, "IRQ wait successful: Oscillator is stable.");
        } else {
            FURI_LOG_W(TAG, "IRQ wait TIMED OUT for oscillator stable.");
        }
    }
    // Disable IRQs
    //furi_hal_nfc_log_irq("Disabling all IRQs", ST25R3916_IRQ_MASK_ALL);
    st25r3916_mask_irq(handle, ST25R3916_IRQ_MASK_ALL);

    bool osc_on = st25r3916_check_reg(
        handle,
        ST25R3916_REG_AUX_DISPLAY,
        ST25R3916_REG_AUX_DISPLAY_osc_ok,
        ST25R3916_REG_AUX_DISPLAY_osc_ok);
    if(!osc_on) {
        FURI_LOG_E(TAG, "Oscillator failed to turn on");
        error = FuriHalNfcErrorOscillator;
    }

    FURI_LOG_D(TAG, "Turn on oscillator complete, status: %d", error);
    //  furi_hal_nfc_relase(); // Removed nested release based on previous analysis
    return error;
}

FuriHalNfcError furi_hal_nfc_is_hal_ready(void) {
    FURI_LOG_D(TAG, "Checking if HAL is ready");
    FuriHalNfcError error = FuriHalNfcErrorNone;

    do {
        error = furi_hal_nfc_acquire();
        if(error != FuriHalNfcErrorNone) break;

        const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
        uint8_t chip_id = 0;
        st25r3916_read_reg(handle, ST25R3916_REG_IC_IDENTITY, &chip_id);
        FURI_LOG_D(TAG, "Read Chip ID: 0x%02X", chip_id);
        if((chip_id & ST25R3916_REG_IC_IDENTITY_ic_type_mask) !=
           ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916) {
            FURI_LOG_E(TAG, "Wrong chip id");
            error = FuriHalNfcErrorCommunication;
        }

        furi_hal_nfc_release();
    } while(false);

    FURI_LOG_D(TAG, "HAL ready check result: %d", error);
    return error;
}

FuriHalNfcError furi_hal_nfc_init(void) {
    furi_check(furi_hal_nfc.mutex == NULL);
    FURI_LOG_D(TAG, "Initializing Furi HAL NFC");

    furi_hal_nfc.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    FuriHalNfcError error = FuriHalNfcErrorNone;

    furi_hal_nfc_event_init();
    furi_hal_nfc_event_start();

    do {
        error = furi_hal_nfc_acquire();
        if(error != FuriHalNfcErrorNone) {
            FURI_LOG_E(TAG, "Failed to acquire NFC mutex during init");
            furi_hal_nfc_low_power_mode_start();
            break;
        }

        const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
        FURI_LOG_D(TAG, "Setting default state");
        st25r3916_direct_cmd(handle, ST25R3916_CMD_SET_DEFAULT);
        FURI_LOG_D(TAG, "Increasing IO driver strength");
        st25r3916_write_reg(handle, ST25R3916_REG_IO_CONF2, ST25R3916_REG_IO_CONF2_io_drv_lvl);
        uint8_t chip_id = 0;
        st25r3916_read_reg(handle, ST25R3916_REG_IC_IDENTITY, &chip_id);
        FURI_LOG_D(TAG, "Read Chip ID: 0x%02X", chip_id);
        if((chip_id & ST25R3916_REG_IC_IDENTITY_ic_type_mask) !=
           ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916) {
            FURI_LOG_E(TAG, "Wrong chip id");
            error = FuriHalNfcErrorCommunication;
            furi_hal_nfc_low_power_mode_start(); // This function contains acquire/release, nested calls.
            furi_hal_nfc_release(); // Release here if we acquired earlier.
            break;
        }
        FURI_LOG_D(TAG, "Clearing and masking interrupts");
        // Clear interrupts
      //  uint32_t pending_irqs = st25r3916_get_irq(handle);
        //furi_hal_nfc_log_irq("Cleared pending IRQs", pending_irqs);
        // Mask all interrupts
        //furi_hal_nfc_log_irq("Masking all IRQs", ST25R3916_IRQ_MASK_ALL);
        st25r3916_mask_irq(handle, ST25R3916_IRQ_MASK_ALL);
        FURI_LOG_D(TAG, "Initializing GPIO ISR");
        // Enable interrupts
        furi_hal_nfc_init_gpio_isr();
        FURI_LOG_D(TAG, "Disabling internal overheat protection");
        // Disable internal overheat protection
        st25r3916_change_test_reg_bits(handle, 0x04, 0x10, 0x10);

        FURI_LOG_D(TAG, "Turning on oscillator");
        error = furi_hal_nfc_turn_on_osc(handle);
        if(error != FuriHalNfcErrorNone) {
            FURI_LOG_E(TAG, "Failed to turn on oscillator");
            furi_hal_nfc_low_power_mode_start(); // This function contains acquire/release, nested calls.
            furi_hal_nfc_release(); // Release here if we acquired earlier.
            break;
        }

        FURI_LOG_D(TAG, "Measuring voltage");
        // Measure voltage
        // Set measure power supply voltage source
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_REGULATOR_CONTROL,
            ST25R3916_REG_REGULATOR_CONTROL_mpsv_mask,
            ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd);
        // Enable timer and interrupt register
        FURI_LOG_D(TAG, "Enable timer and interrupt register");
        uint32_t irq_to_enable = ST25R3916_IRQ_MASK_DCT;
        //furi_hal_nfc_log_irq("Enabling IRQ for measurement", irq_to_enable);
        st25r3916_mask_irq(handle, ~irq_to_enable);
        st25r3916_direct_cmd(handle, ST25R3916_CMD_MEASURE_VDD);
        FURI_LOG_D(TAG, "furi_hal_nfc_event_wait_for_specific_irq");
        //furi_hal_nfc_log_irq("Waiting for specific IRQ", ST25R3916_IRQ_MASK_DCT);
        if(furi_hal_nfc_event_wait_for_specific_irq(handle, ST25R3916_IRQ_MASK_DCT, 100)) {
            FURI_LOG_D(TAG, "IRQ wait successful: Direct command finished.");
        } else {
            FURI_LOG_W(TAG, "IRQ wait TIMED OUT for direct command.");
        }
        //furi_hal_nfc_log_irq("Masking all IRQs", ST25R3916_IRQ_MASK_ALL);
        st25r3916_mask_irq(handle, ST25R3916_IRQ_MASK_ALL);
        uint8_t ad_res = 0;
        st25r3916_read_reg(handle, ST25R3916_REG_AD_RESULT, &ad_res);
        uint16_t mV = ((uint16_t)ad_res) * 23U;
        mV += (((((uint16_t)ad_res) * 4U) + 5U) / 10U);
        FURI_LOG_D(TAG, "Measured voltage: %u mV", mV);

        if(mV < 3600) {
            FURI_LOG_D(TAG, "Setting supply voltage to 3V mode");
            st25r3916_change_reg_bits(
                handle,
                ST25R3916_REG_IO_CONF2,
                ST25R3916_REG_IO_CONF2_sup3V,
                ST25R3916_REG_IO_CONF2_sup3V_3V);
        } else {
            FURI_LOG_D(TAG, "Setting supply voltage to 5V mode");
            st25r3916_change_reg_bits(
                handle,
                ST25R3916_REG_IO_CONF2,
                ST25R3916_REG_IO_CONF2_sup3V,
                ST25R3916_REG_IO_CONF2_sup3V_5V);
        }

        FURI_LOG_D(TAG, "Applying misc config settings");
        // Disable MCU CLK
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_IO_CONF1,
            ST25R3916_REG_IO_CONF1_out_cl_mask | ST25R3916_REG_IO_CONF1_lf_clk_off,
            0x07);
        // Disable MISO pull-down
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_IO_CONF2,
            ST25R3916_REG_IO_CONF2_miso_pd1 | ST25R3916_REG_IO_CONF2_miso_pd2,
            0x00);
        // Set tx driver resistance to 1 Om
        st25r3916_change_reg_bits(
            handle, ST25R3916_REG_TX_DRIVER, ST25R3916_REG_TX_DRIVER_d_res_mask, 0x00);
        // Use minimum non-overlap
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_RES_AM_MOD,
            ST25R3916_REG_RES_AM_MOD_fa3_f,
            ST25R3916_REG_RES_AM_MOD_fa3_f);

        // Set activation threashold
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_FIELD_THRESHOLD_ACTV,
            ST25R3916_REG_FIELD_THRESHOLD_ACTV_trg_mask,
            ST25R3916_REG_FIELD_THRESHOLD_ACTV_trg_105mV);
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_FIELD_THRESHOLD_ACTV,
            ST25R3916_REG_FIELD_THRESHOLD_ACTV_rfe_mask,
            ST25R3916_REG_FIELD_THRESHOLD_ACTV_rfe_105mV);
        // Set deactivation threashold
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_FIELD_THRESHOLD_DEACTV,
            ST25R3916_REG_FIELD_THRESHOLD_DEACTV_trg_mask,
            ST25R3916_REG_FIELD_THRESHOLD_DEACTV_trg_75mV);
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_FIELD_THRESHOLD_DEACTV,
            ST25R3916_REG_FIELD_THRESHOLD_DEACTV_rfe_mask,
            ST25R3916_REG_FIELD_THRESHOLD_DEACTV_rfe_75mV);
        // Enable external load modulation
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_AUX_MOD,
            ST25R3916_REG_AUX_MOD_lm_ext,
            ST25R3916_REG_AUX_MOD_lm_ext);
        // Enable internal load modulation
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_AUX_MOD,
            ST25R3916_REG_AUX_MOD_lm_dri,
            ST25R3916_REG_AUX_MOD_lm_dri);
        // Adjust FDT
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_PASSIVE_TARGET,
            ST25R3916_REG_PASSIVE_TARGET_fdel_mask,
            (5U << ST25R3916_REG_PASSIVE_TARGET_fdel_shift));
        // Reduce RFO resistance in Modulated state
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_PT_MOD,
            ST25R3916_REG_PT_MOD_ptm_res_mask | ST25R3916_REG_PT_MOD_pt_res_mask,
            0x0f);
        // Enable RX start on first 4 bits
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_EMD_SUP_CONF,
            ST25R3916_REG_EMD_SUP_CONF_rx_start_emv,
            ST25R3916_REG_EMD_SUP_CONF_rx_start_emv_on);
        // Set antena tunning
        st25r3916_change_reg_bits(handle, ST25R3916_REG_ANT_TUNE_A, 0xff, 0x82);
        st25r3916_change_reg_bits(handle, ST25R3916_REG_ANT_TUNE_B, 0xff, 0x82);
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_OP_CONTROL,
            ST25R3916_REG_OP_CONTROL_en_fd_mask,
            ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);

        // Perform calibration
        if(st25r3916_check_reg(
               handle,
               ST25R3916_REG_REGULATOR_CONTROL,
               ST25R3916_REG_REGULATOR_CONTROL_reg_s,
               0x00)) {
            FURI_LOG_D(TAG, "Adjusting regulators");
            // Reset logic
            st25r3916_set_reg_bits(
                handle, ST25R3916_REG_REGULATOR_CONTROL, ST25R3916_REG_REGULATOR_CONTROL_reg_s);
            st25r3916_clear_reg_bits(
                handle, ST25R3916_REG_REGULATOR_CONTROL, ST25R3916_REG_REGULATOR_CONTROL_reg_s);
            st25r3916_direct_cmd(handle, ST25R3916_CMD_ADJUST_REGULATORS);
            furi_delay_ms(6);
        }

        FURI_LOG_D(TAG, "Initialization successful, entering low power mode");

        furi_hal_nfc_low_power_mode_start();
        furi_hal_nfc_release();

    } while(false);

    return error;
}

static bool furi_hal_nfc_is_mine(void) {
    return furi_mutex_get_owner(furi_hal_nfc.mutex) == furi_thread_get_current_id();
}

FuriHalNfcError furi_hal_nfc_acquire(void) {
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc.mutex);
    if(!furi_hal_nfc_is_mine()) {
        FURI_LOG_T(TAG, "Acquiring NFC");

        furi_hal_spi_acquire(&furi_hal_spi_bus_handle_nfc);

        if(furi_mutex_acquire(furi_hal_nfc.mutex, 100) != FuriStatusOk) {
            furi_hal_spi_release(&furi_hal_spi_bus_handle_nfc);
            FURI_LOG_W(TAG, "Failed to acquire mutex, NFC busy");
            error = FuriHalNfcErrorBusy;
        }
    }
    return error;
}

FuriHalNfcError furi_hal_nfc_release(void) {
    furi_check(furi_hal_nfc.mutex);
    if(furi_hal_nfc_is_mine()) {
        FURI_LOG_T(TAG, "Releasing NFC");
        furi_check(furi_mutex_release(furi_hal_nfc.mutex) == FuriStatusOk);

        furi_hal_spi_release(&furi_hal_spi_bus_handle_nfc);
    }
    return FuriHalNfcErrorNone;
}
FuriHalNfcError furi_hal_nfc_low_power_mode_start(void) {
    FURI_LOG_D(TAG, "Entering low power mode");
    FuriHalNfcError error = FuriHalNfcErrorNone;
    // Removed nested acquire to prevent reentrancy issues.
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);

    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    st25r3916_direct_cmd(handle, ST25R3916_CMD_STOP);
    st25r3916_clear_reg_bits(
        handle,
        ST25R3916_REG_OP_CONTROL,
        (ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_rx_en |
         ST25R3916_REG_OP_CONTROL_wu | ST25R3916_REG_OP_CONTROL_tx_en |
         ST25R3916_REG_OP_CONTROL_en_fd_mask));
    furi_hal_nfc_deinit_gpio_isr();
    furi_hal_nfc_timers_deinit();
    furi_hal_nfc_event_stop();
    furi_hal_nfc_release();

    return error;
}

FuriHalNfcError furi_hal_nfc_low_power_mode_stop(void) {
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_hal_nfc_acquire(); // [Acquire X]

    FURI_LOG_D(TAG, "Exiting low power mode");
    //furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone); // Dead code or commented out logic indicating a nested acquire issue
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    do {
        furi_hal_nfc_init_gpio_isr();
        furi_hal_nfc_timers_init();
        error = furi_hal_nfc_turn_on_osc(handle);
        if(error != FuriHalNfcErrorNone) break;
        st25r3916_change_reg_bits(
            handle,
            ST25R3916_REG_OP_CONTROL,
            ST25R3916_REG_OP_CONTROL_en_fd_mask,
            ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);

    } while(false);

    FURI_LOG_D(TAG, "Exit low power mode complete, status: %d", error);
    furi_hal_nfc_release(); // [Release X]
    return error;
}

static FuriHalNfcError furi_hal_nfc_poller_init_common(const FuriHalSpiBusHandle* handle) {
    FURI_LOG_D(TAG, "npi_c 1: Common poller initialization start");

    //  furi_hal_nfc_acquire(); // Removed nested acquire based on previous analysis
    // FURI_LOG_D(TAG, "npi_c 2: furi_hal_nfc_acquire success");

    // Disable wake up
    st25r3916_clear_reg_bits(handle, ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_wu);
    FURI_LOG_D(TAG, "npi_c 3: ST25R3916_REG_OP_CONTROL written");

    // Enable correlator
    st25r3916_change_reg_bits(
        handle,
        ST25R3916_REG_AUX,
        ST25R3916_REG_AUX_dis_corr,
        ST25R3916_REG_AUX_dis_corr_correlator);
    FURI_LOG_D(TAG, "npi_c 4: ST25R3916_REG_AUX written (correlator)");

    st25r3916_change_reg_bits(handle, ST25R3916_REG_ANT_TUNE_A, 0xff, 0x82);
    FURI_LOG_D(TAG, "npi_c 5: ST25R3916_REG_ANT_TUNE_A written");

    st25r3916_change_reg_bits(handle, ST25R3916_REG_ANT_TUNE_B, 0xFF, 0x82);
    FURI_LOG_D(TAG, "npi_c 6: ST25R3916_REG_ANT_TUNE_B written");

    st25r3916_write_reg(handle, ST25R3916_REG_OVERSHOOT_CONF1, 0x00);
    FURI_LOG_D(TAG, "npi_c 7: ST25R3916_REG_OVERSHOOT_CONF1 written");

    st25r3916_write_reg(handle, ST25R3916_REG_OVERSHOOT_CONF2, 0x00);
    FURI_LOG_D(TAG, "npi_c 8: ST25R3916_REG_OVERSHOOT_CONF2 written");

    st25r3916_write_reg(handle, ST25R3916_REG_UNDERSHOOT_CONF1, 0x00);
    FURI_LOG_D(TAG, "npi_c 9: ST25R3916_REG_UNDERSHOOT_CONF1 written");

    st25r3916_write_reg(handle, ST25R3916_REG_UNDERSHOOT_CONF2, 0x00);
    FURI_LOG_D(TAG, "npi_c 10: ST25R3916_REG_UNDERSHOOT_CONF2 written");

    //   furi_hal_nfc_release(); // Removed nested release based on previous analysis
    FURI_LOG_D(TAG, "npi_c 11: furi_hal_nfc_release success");

    return FuriHalNfcErrorNone;
}

static FuriHalNfcError furi_hal_nfc_listener_init_common(const FuriHalSpiBusHandle* handle) {
    FURI_LOG_D(TAG, "Common listener initialization");
    UNUSED(handle);
    return FuriHalNfcErrorNone;
}

FuriHalNfcError furi_hal_nfc_set_mode(FuriHalNfcMode mode, FuriHalNfcTech tech) {
    furi_check(mode < FuriHalNfcModeNum);
    furi_check(tech < FuriHalNfcTechNum);
    FURI_LOG_D(TAG, "Setting mode: %d, tech: %d", mode, tech);
    //  furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    furi_hal_nfc_acquire();
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    FuriHalNfcError error = FuriHalNfcErrorNone;
    error = furi_hal_nfc_poller_init_common(handle);
    if(mode == FuriHalNfcModePoller) {
        FURI_LOG_D(TAG, "Initializing as Poller");
        do {
            if(error != FuriHalNfcErrorNone) {
                FURI_LOG_D(TAG, "break");
                break;
            }

            error = furi_hal_nfc_tech[tech]->poller.init(handle);
        } while(false);

    } else if(mode == FuriHalNfcModeListener) {
        FURI_LOG_D(TAG, "Initializing as Listener");
        do {
            error = furi_hal_nfc_listener_init_common(handle);
            if(error != FuriHalNfcErrorNone) break;
            error = furi_hal_nfc_tech[tech]->listener.init(handle);
        } while(false);
    }

    furi_hal_nfc.mode = mode;
    furi_hal_nfc.tech = tech;
    FURI_LOG_D(TAG, "Set mode finished, status: %d", error);
    furi_hal_nfc_release();
    return error;
}

FuriHalNfcError furi_hal_nfc_reset_mode(void) {
    FURI_LOG_D(TAG, "Resetting mode");
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    st25r3916_direct_cmd(handle, ST25R3916_CMD_STOP);

    const FuriHalNfcMode mode = furi_hal_nfc.mode;
    const FuriHalNfcTech tech = furi_hal_nfc.tech;
    if(mode == FuriHalNfcModePoller) {
        FURI_LOG_D(TAG, "De-initializing poller for tech %d", tech);
        error = furi_hal_nfc_tech[tech]->poller.deinit(handle);
    } else if(mode == FuriHalNfcModeListener) {
        FURI_LOG_D(TAG, "De-initializing listener for tech %d", tech);
        error = furi_hal_nfc_tech[tech]->listener.deinit(handle);
    }
    FURI_LOG_D(TAG, "Restoring default register values");
    // Set default value in mode register
    st25r3916_write_reg(handle, ST25R3916_REG_MODE, ST25R3916_REG_MODE_om0);
    st25r3916_write_reg(handle, ST25R3916_REG_STREAM_MODE, 0);
    st25r3916_clear_reg_bits(handle, ST25R3916_REG_AUX, ST25R3916_REG_AUX_no_crc_rx);
    st25r3916_clear_reg_bits(
        handle,
        ST25R3916_REG_BIT_RATE,
        ST25R3916_REG_BIT_RATE_txrate_mask | ST25R3916_REG_BIT_RATE_rxrate_mask);

    // Write default values
    st25r3916_write_reg(handle, ST25R3916_REG_RX_CONF1, 0);
    st25r3916_write_reg(
        handle,
        ST25R3916_REG_RX_CONF2,
        ST25R3916_REG_RX_CONF2_sqm_dyn | ST25R3916_REG_RX_CONF2_agc_en |
            ST25R3916_REG_RX_CONF2_agc_m);

    st25r3916_write_reg(
        handle,
        ST25R3916_REG_CORR_CONF1,
        ST25R3916_REG_CORR_CONF1_corr_s7 | ST25R3916_REG_CORR_CONF1_corr_s4 |
            ST25R3916_REG_CORR_CONF1_corr_s1 | ST25R3916_REG_CORR_CONF1_corr_s0);
    st25r3916_write_reg(handle, ST25R3916_REG_CORR_CONF2, 0);
    furi_hal_nfc_release();
    return error;
}

FuriHalNfcError furi_hal_nfc_field_detect_start(void) {
    FURI_LOG_D(TAG, "Starting field detection");
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    st25r3916_write_reg(
        handle,
        ST25R3916_REG_OP_CONTROL,
        ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_en_fd_mask);
    st25r3916_write_reg(
        handle, ST25R3916_REG_MODE, ST25R3916_REG_MODE_targ | ST25R3916_REG_MODE_om0);
    furi_hal_nfc_release();
    return error;
}

FuriHalNfcError furi_hal_nfc_field_detect_stop(void) {
    FURI_LOG_D(TAG, "Stopping field detection");
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    st25r3916_clear_reg_bits(
        handle,
        ST25R3916_REG_OP_CONTROL,
        (ST25R3916_REG_OP_CONTROL_en | ST25R3916_REG_OP_CONTROL_en_fd_mask));
    furi_hal_nfc_release();
    return error;
}

bool furi_hal_nfc_field_is_present(void) {
    FURI_LOG_T(TAG, "Checking for external field presence");
    bool is_present = false;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    if(st25r3916_check_reg(
           handle,
           ST25R3916_REG_AUX_DISPLAY,
           ST25R3916_REG_AUX_DISPLAY_efd_o,
           ST25R3916_REG_AUX_DISPLAY_efd_o)) {
        is_present = true;
    }

    FURI_LOG_T(TAG, "Field is present: %s", is_present ? "true" : "false");
    furi_hal_nfc_release();
    return is_present;
}

FuriHalNfcError furi_hal_nfc_poller_field_on(void) {
    FURI_LOG_T(TAG, "Turning poller field on");
    FuriHalNfcError error = FuriHalNfcErrorNone;

    // Acquire the lock ONCE at the beginning of the function
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);

    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    // The second, problematic acquire has been removed.

    if(!st25r3916_check_reg(
           handle,
           ST25R3916_REG_OP_CONTROL,
           ST25R3916_REG_OP_CONTROL_tx_en,
           ST25R3916_REG_OP_CONTROL_tx_en)) {
        // Set min guard time
        st25r3916_write_reg(handle, ST25R3916_REG_FIELD_ON_GT, 0);
        // Enable tx rx
        st25r3916_set_reg_bits(
            handle,
            ST25R3916_REG_OP_CONTROL,
            (ST25R3916_REG_OP_CONTROL_rx_en | ST25R3916_REG_OP_CONTROL_tx_en));
    }

    // Release the lock ONCE at the end of the function
    furi_hal_nfc_release();
    return error;
}

FuriHalNfcError furi_hal_nfc_poller_tx_common(
    const FuriHalSpiBusHandle* handle,
    const uint8_t* tx_data,
    size_t tx_bits) {
    furi_check(tx_data);
    FURI_LOG_T(TAG, "Poller common TX, %zu bits", tx_bits);

    FuriHalNfcError err = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    // Prepare tx
    st25r3916_direct_cmd(handle, ST25R3916_CMD_CLEAR_FIFO);
    st25r3916_clear_reg_bits(
        handle, ST25R3916_REG_TIMER_EMV_CONTROL, ST25R3916_REG_TIMER_EMV_CONTROL_nrt_emv);
    st25r3916_change_reg_bits(
        handle,
        ST25R3916_REG_ISO14443A_NFC,
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par | ST25R3916_REG_ISO14443A_NFC_no_rx_par),
        (ST25R3916_REG_ISO14443A_NFC_no_tx_par_off | ST25R3916_REG_ISO14443A_NFC_no_rx_par_off));
    uint32_t interrupts =
        (ST25R3916_IRQ_MASK_FWL | ST25R3916_IRQ_MASK_TXE | ST25R3916_IRQ_MASK_RXS |
         ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC |
         ST25R3916_IRQ_MASK_ERR1 | ST25R3916_IRQ_MASK_ERR2 | ST25R3916_IRQ_MASK_NRE);
    // Clear interrupts
    //uint32_t pending_irqs = st25r3916_get_irq(handle);
    //furi_hal_nfc_log_irq("Cleared pending IRQs before TX", pending_irqs);
    // Enable interrupts
    //furi_hal_nfc_log_irq("Enabling IRQs for TX/RX", interrupts);
    st25r3916_mask_irq(handle, ~interrupts);

    st25r3916_write_fifo(handle, tx_data, tx_bits);
    st25r3916_direct_cmd(handle, ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
    furi_hal_nfc_release();
    return err;
}

FuriHalNfcError furi_hal_nfc_common_fifo_tx(
    const FuriHalSpiBusHandle* handle,
    const uint8_t* tx_data,
    size_t tx_bits) {
    FURI_LOG_T(TAG, "Common FIFO TX, %zu bits", tx_bits);
    FuriHalNfcError err = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    st25r3916_direct_cmd(handle, ST25R3916_CMD_CLEAR_FIFO);
    st25r3916_write_fifo(handle, tx_data, tx_bits);
    st25r3916_direct_cmd(handle, ST25R3916_CMD_TRANSMIT_WITHOUT_CRC);
    furi_hal_nfc_release();
    return err;
}

FuriHalNfcError furi_hal_nfc_poller_tx(const uint8_t* tx_data, size_t tx_bits) {
    furi_check(furi_hal_nfc.mode == FuriHalNfcModePoller);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_T(TAG, "Poller TX for tech %d", furi_hal_nfc.tech);
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
    furi_hal_nfc_release();
    return furi_hal_nfc_tech[furi_hal_nfc.tech]->poller.tx(handle, tx_data, tx_bits);
}

FuriHalNfcError furi_hal_nfc_poller_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits) {
    furi_check(furi_hal_nfc.mode == FuriHalNfcModePoller);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_T(TAG, "Poller RX for tech %d", furi_hal_nfc.tech);
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);

    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
    furi_hal_nfc_release();

    return furi_hal_nfc_tech[furi_hal_nfc.tech]->poller.rx(handle, rx_data, rx_data_size, rx_bits);
}

FuriHalNfcEvent furi_hal_nfc_poller_wait_event(uint32_t timeout_ms) {
    furi_check(furi_hal_nfc.mode == FuriHalNfcModePoller);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_T(TAG, "Poller wait event for tech %d", furi_hal_nfc.tech);
    return furi_hal_nfc_tech[furi_hal_nfc.tech]->poller.wait_event(timeout_ms);
}

FuriHalNfcEvent furi_hal_nfc_listener_wait_event(uint32_t timeout_ms) {
    furi_check(furi_hal_nfc.mode == FuriHalNfcModeListener);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_T(TAG, "Listener wait event for tech %d", furi_hal_nfc.tech);

    return furi_hal_nfc_tech[furi_hal_nfc.tech]->listener.wait_event(timeout_ms);
}

FuriHalNfcError furi_hal_nfc_listener_tx(const uint8_t* tx_data, size_t tx_bits) {
    furi_check(tx_data);

    furi_check(furi_hal_nfc.mode == FuriHalNfcModeListener);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_T(TAG, "Listener TX for tech %d", furi_hal_nfc.tech);
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);

    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    furi_hal_nfc_release();

    return furi_hal_nfc_tech[furi_hal_nfc.tech]->listener.tx(handle, tx_data, tx_bits);
}

FuriHalNfcError furi_hal_nfc_common_fifo_rx(
    const FuriHalSpiBusHandle* handle,
    uint8_t* rx_data,
    size_t rx_data_size,
    size_t* rx_bits) {
    FURI_LOG_T(TAG, "Common FIFO RX");
    FuriHalNfcError error = FuriHalNfcErrorNone;
    furi_check(furi_hal_nfc_acquire() == FuriHalNfcErrorNone);
    if(!st25r3916_read_fifo(handle, rx_data, rx_data_size, rx_bits)) {
        FURI_LOG_W(TAG, "FIFO RX buffer overflow");
        error = FuriHalNfcErrorBufferOverflow;
    }
    FURI_LOG_T(TAG, "Read %zu bits from FIFO", *rx_bits);
    furi_hal_nfc_release();
    return error;
}

FuriHalNfcError furi_hal_nfc_listener_rx(uint8_t* rx_data, size_t rx_data_size, size_t* rx_bits) {
    furi_check(rx_data);
    furi_check(rx_bits);

    furi_check(furi_hal_nfc.mode == FuriHalNfcModeListener);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_T(TAG, "Listener RX for tech %d", furi_hal_nfc.tech);
    furi_hal_nfc_acquire();
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
    return furi_hal_nfc_tech[furi_hal_nfc.tech]->listener.rx(
        handle, rx_data, rx_data_size, rx_bits);
}

FuriHalNfcError furi_hal_nfc_trx_reset(void) {
    FURI_LOG_D(TAG, "Resetting TRX");
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;
    FURI_LOG_D(TAG, "Resetting TRX");
    st25r3916_direct_cmd(handle, ST25R3916_CMD_STOP);
    FURI_LOG_D(TAG, "Resetting TRX");
    return FuriHalNfcErrorNone;
}

FuriHalNfcError furi_hal_nfc_listener_sleep(void) {
    furi_check(furi_hal_nfc.mode == FuriHalNfcModeListener);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_D(TAG, "Listener entering sleep state for tech %d", furi_hal_nfc.tech);

    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    return furi_hal_nfc_tech[furi_hal_nfc.tech]->listener.sleep(handle);
}

FuriHalNfcError furi_hal_nfc_listener_idle(void) {
    furi_check(furi_hal_nfc.mode == FuriHalNfcModeListener);
    furi_check(furi_hal_nfc.tech < FuriHalNfcTechNum);
    FURI_LOG_D(TAG, "Listener entering idle state for tech %d", furi_hal_nfc.tech);

    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    return furi_hal_nfc_tech[furi_hal_nfc.tech]->listener.idle(handle);
}

FuriHalNfcError furi_hal_nfc_listener_enable_rx(void) {
    FURI_LOG_D(TAG, "Listener enabling RX (unmasking)");
    const FuriHalSpiBusHandle* handle = &furi_hal_spi_bus_handle_nfc;

    st25r3916_direct_cmd(handle, ST25R3916_CMD_UNMASK_RECEIVE_DATA);

    return FuriHalNfcErrorNone;
}
