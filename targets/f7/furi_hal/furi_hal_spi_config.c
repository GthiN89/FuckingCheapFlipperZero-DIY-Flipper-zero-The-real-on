#include <furi_hal_spi_config.h>
#include <furi_hal_resources.h> // Provides GPIO pin definitions
#include <furi_hal_spi.h> // Provides SPI HAL functions and types
#include <furi_hal_bus.h> // For bus clock enable/disable
#include <furi.h>

// Include necessary LL drivers
#include <stm32wbxx_ll_spi.h>
#include <stm32wbxx_ll_gpio.h>

#define TAG "FuriHalSpiConfig"



// const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_8m = {
//     .TransferDirection = LL_SPI_FULL_DUPLEX,
//     .Mode = LL_SPI_MODE_MASTER,
//     .DataWidth = LL_SPI_DATAWIDTH_8BIT,
//     .ClockPolarity = LL_SPI_POLARITY_LOW,
//     .ClockPhase = LL_SPI_PHASE_1EDGE, // Mode 0
//     .NSS = LL_SPI_NSS_SOFT,
//     .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV32, // ~8 MHz @ 64MHz Clock
//     .BitOrder = LL_SPI_MSB_FIRST,
//     .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
//     .CRCPoly = 7,
// };

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_8m_NFC = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_2EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV64,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_2edge_low_8m = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_2EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV64,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_8m = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_1EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV64,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_BTN = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_HIGH, // Changed from LOW
    .ClockPhase = LL_SPI_PHASE_2EDGE,      // Changed from 1EDGE
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV64,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_4m = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_1EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV32,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_16m = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_1EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV64,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_LCD_2m = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_1EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV128,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

const LL_SPI_InitTypeDef furi_hal_spi_preset_1edge_low_2m = {
    .Mode = LL_SPI_MODE_MASTER,
    .TransferDirection = LL_SPI_FULL_DUPLEX,
    .DataWidth = LL_SPI_DATAWIDTH_8BIT,
    .ClockPolarity = LL_SPI_POLARITY_LOW,
    .ClockPhase = LL_SPI_PHASE_1EDGE,
    .NSS = LL_SPI_NSS_SOFT,
    .BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV256,
    .BitOrder = LL_SPI_MSB_FIRST,
    .CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
    .CRCPoly = 7,
};

/* ========================== SPI Bus Definition (Single Bus) =========================== */

//Define ONE bus for SPI1, keeping the name furi_hal_spi_bus
FuriMutex* furi_hal_spi_bus_mutex = NULL;

void furi_hal_spi_config_init_early(void) {
    furi_hal_spi_bus_init(&furi_hal_spi_bus);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_display);
}

void furi_hal_spi_config_deinit_early(void) {
    furi_hal_spi_bus_handle_deinit(&furi_hal_spi_bus_handle_display);
    furi_hal_spi_bus_init(&furi_hal_spi_bus);
}

void furi_hal_spi_config_init(void) {
    furi_hal_spi_bus_init(&furi_hal_spi_bus);

    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_subghz);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_nfc);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_sd_fast);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_sd_slow);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_button_sr);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_external);
    furi_hal_spi_bus_handle_init(&furi_hal_spi_bus_handle_external_extra);

    //   FURI_LOG_I(TAG, "Init OK");
}

static void furi_hal_spi_bus_event_callback(FuriHalSpiBus* bus, FuriHalSpiBusEvent event) {
    if(event == FuriHalSpiBusEventInit) {
        furi_hal_spi_bus_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        bus->current_handle = NULL;
    } else if(event == FuriHalSpiBusEventDeinit) {
        furi_mutex_free(furi_hal_spi_bus_mutex);
    } else if(event == FuriHalSpiBusEventLock) {
        furi_check(furi_mutex_acquire(furi_hal_spi_bus_mutex, FuriWaitForever) == FuriStatusOk);
    } else if(event == FuriHalSpiBusEventUnlock) {
        furi_check(furi_mutex_release(furi_hal_spi_bus_mutex) == FuriStatusOk);
    } else if(event == FuriHalSpiBusEventActivate) {
        furi_hal_bus_enable(FuriHalBusSPI1);
    } else if(event == FuriHalSpiBusEventDeactivate) {
        furi_hal_bus_disable(FuriHalBusSPI1);
    }
}


// The single SPI bus structure for SPI1, named furi_hal_spi_bus
FuriHalSpiBus furi_hal_spi_bus = {
    .spi = SPI1,
    .callback = furi_hal_spi_bus_event_callback,
    .current_handle = NULL,
};

/* ===================== SPI Bus Handle Event Callbacks ===================== */

#define SPI1_GPIO_ALT_FN LL_GPIO_AF_5

inline static void furi_hal_spi_bus_external_handle_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event,
    const LL_SPI_InitTypeDef* preset) {
    if(event == FuriHalSpiBusHandleEventInit) {
        furi_hal_gpio_write(handle->cs, true);
        furi_hal_gpio_init(handle->cs, GpioModeOutputPushPull, GpioPullUp, GpioSpeedVeryHigh);
    } else if(event == FuriHalSpiBusHandleEventDeinit) {
        furi_hal_gpio_write(handle->cs, true);
        furi_hal_gpio_init(handle->cs, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    } else if(event == FuriHalSpiBusHandleEventActivate) {
        LL_SPI_Init(handle->bus->spi, (LL_SPI_InitTypeDef*)preset);
        LL_SPI_SetRxFIFOThreshold(handle->bus->spi, LL_SPI_RX_FIFO_TH_QUARTER);
        LL_SPI_Enable(handle->bus->spi);

        furi_hal_gpio_init_ex(
            handle->miso,
            GpioModeAltFunctionPushPull,
            GpioPullDown,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->mosi,
            GpioModeAltFunctionPushPull,
            GpioPullDown,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->sck,
            GpioModeAltFunctionPushPull,
            GpioPullDown,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);

        furi_hal_gpio_write(handle->cs, false);
    } else if(event == FuriHalSpiBusHandleEventDeactivate) {
        furi_hal_gpio_write(handle->cs, true);

        furi_hal_gpio_init(handle->miso, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->mosi, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->sck, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

        LL_SPI_Disable(handle->bus->spi);
    }
}

inline static void furi_hal_spi_bus_nfc_handle_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event,
    const LL_SPI_InitTypeDef* preset) {
    if(event == FuriHalSpiBusHandleEventInit) {
        // Configure GPIOs in normal SPI mode
        furi_hal_gpio_init_ex(
            handle->miso,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->mosi,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->sck,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_write(handle->cs, true);
        furi_hal_gpio_init(handle->cs, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    } else if(event == FuriHalSpiBusHandleEventDeinit) {
        // Configure GPIOs for st25r3916 Transparent mode
        furi_hal_gpio_init(handle->sck, GpioModeInput, GpioPullUp, GpioSpeedLow);
        furi_hal_gpio_init(handle->miso, GpioModeInput, GpioPullUp, GpioSpeedLow);
        furi_hal_gpio_init(handle->cs, GpioModeInput, GpioPullUp, GpioSpeedLow);
        furi_hal_gpio_write(handle->mosi, false);
        furi_hal_gpio_init(handle->mosi, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    } else if(event == FuriHalSpiBusHandleEventActivate) {
        LL_SPI_Init(handle->bus->spi, (LL_SPI_InitTypeDef*)preset);
        LL_SPI_SetRxFIFOThreshold(handle->bus->spi, LL_SPI_RX_FIFO_TH_QUARTER);
        LL_SPI_Enable(handle->bus->spi);

        furi_hal_gpio_init_ex(
            handle->miso,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->mosi,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->sck,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);

    } else if(event == FuriHalSpiBusHandleEventDeactivate) {
        furi_hal_gpio_init(handle->miso, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->mosi, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->sck, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

        LL_SPI_Disable(handle->bus->spi);
    }
}

/**
 * @brief Generic event callback for standard SPI devices using SPI1 bus.
 */
inline static void furi_hal_spi_bus_generic_handle_event_callback(
        const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event,
    const LL_SPI_InitTypeDef* preset) {
    if(event == FuriHalSpiBusHandleEventInit) {
        furi_hal_gpio_write(handle->cs, true);
        furi_hal_gpio_init(handle->cs, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    } else if(event == FuriHalSpiBusHandleEventDeinit) {
        furi_hal_gpio_write(handle->cs, true);
        furi_hal_gpio_init(handle->cs, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    } else if(event == FuriHalSpiBusHandleEventActivate) {
        LL_SPI_Init(handle->bus->spi, (LL_SPI_InitTypeDef*)preset);
        LL_SPI_SetRxFIFOThreshold(handle->bus->spi, LL_SPI_RX_FIFO_TH_QUARTER);
        LL_SPI_Enable(handle->bus->spi);

        furi_hal_gpio_init_ex(
            handle->miso,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->mosi,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        furi_hal_gpio_init_ex(
            handle->sck,
            GpioModeAltFunctionPushPull,
            GpioPullNo,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);

        furi_hal_gpio_write(handle->cs, false);
    } else if(event == FuriHalSpiBusHandleEventDeactivate) {
        furi_hal_gpio_write(handle->cs, true);

        furi_hal_gpio_init(handle->miso, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->mosi, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->sck, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

        LL_SPI_Disable(handle->bus->spi);
    }
}




/**
 * @brief Event callback for external SPI handles.
 * Assumes shared SPI pins but potentially different GPIO pull settings if needed.
 * Defined before its wrapper.
 */

static void furi_hal_spi_bus_handle_button_sr_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    // Use a standard speed, 4MHz is plenty fast for buttons
    const LL_SPI_InitTypeDef* preset = &furi_hal_spi_preset_1edge_low_BTN; 

    if(event == FuriHalSpiBusHandleEventInit) {
        // Initialize our LATCH pin (which is handle->cs) as an output
        furi_hal_gpio_write(handle->cs, true); // Latch normally high
        furi_hal_gpio_init(handle->cs, GpioModeOutputPushPull, GpioPullUp, GpioSpeedLow);
    } else if(event == FuriHalSpiBusHandleEventDeinit) {
        // Reset LATCH pin to analog
        furi_hal_gpio_init(handle->cs, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    } else if(event == FuriHalSpiBusHandleEventActivate) {
        // Standard SPI pin activation
        LL_SPI_Init(handle->bus->spi, (LL_SPI_InitTypeDef*)preset);
        LL_SPI_Enable(handle->bus->spi);
        furi_hal_gpio_init_ex(handle->miso, GpioModeAltFunctionPushPull, GpioPullUp, GpioSpeedVeryHigh, SPI1_GPIO_ALT_FN);
        furi_hal_gpio_init_ex(handle->mosi, GpioModeAltFunctionPushPull, GpioPullUp, GpioSpeedVeryHigh, SPI1_GPIO_ALT_FN);
        furi_hal_gpio_init_ex(handle->sck, GpioModeAltFunctionPushPull, GpioPullUp, GpioSpeedVeryHigh, SPI1_GPIO_ALT_FN);
        // We DO NOT assert CS here because our service code does it manually
    } else if(event == FuriHalSpiBusHandleEventDeactivate) {
        // We DO NOT de-assert CS here
        // Standard SPI pin deactivation
        while(LL_SPI_IsActiveFlag_BSY(handle->bus->spi)) {};
        LL_SPI_Disable(handle->bus->spi);
        furi_hal_gpio_init(handle->miso, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->mosi, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_init(handle->sck, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    }
}



void furi_hal_spi_bus_handle_display_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    // Using the extra slow preset for display
    furi_hal_spi_bus_generic_handle_event_callback(
        handle, event, &furi_hal_spi_preset_1edge_LCD_2m);
}

static void furi_hal_spi_bus_handle_sd_fast_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    furi_hal_spi_bus_generic_handle_event_callback(
        handle, event, &furi_hal_spi_preset_1edge_low_16m);
}

static void furi_hal_spi_bus_handle_sd_slow_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    furi_hal_spi_bus_generic_handle_event_callback(
        handle, event, &furi_hal_spi_preset_1edge_low_2m);
}

static void furi_hal_spi_bus_handle_subghz_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    furi_hal_spi_bus_generic_handle_event_callback(
        handle, event, &furi_hal_spi_preset_1edge_low_8m);
}

static void furi_hal_spi_bus_handle_nfc_wrapper_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    furi_hal_spi_bus_nfc_handle_event_callback(handle, event, &furi_hal_spi_preset_1edge_low_8m_NFC);
}

// Wrapper for External handles - calls the specific external callback implementation
static void furi_hal_spi_bus_handle_external_wrapper_event_callback(
    const FuriHalSpiBusHandle* handle,
    FuriHalSpiBusHandleEvent event) {
    furi_hal_spi_bus_external_handle_event_callback(
        handle, event, &furi_hal_spi_preset_1edge_LCD_2m);
}

/* ======================== SPI Bus Handle Definitions ======================= */

const FuriHalSpiBusHandle furi_hal_spi_bus_handle_display = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_display_event_callback,
    .miso = &gpio_spi_miso,
    .mosi = &gpio_spi_mosi,
    .sck = &gpio_spi_sck,
    .cs = &gpio_display_cs,
};

const FuriHalSpiBusHandle furi_hal_spi_bus_handle_sd_fast = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_sd_fast_event_callback,
    .miso = &gpio_spi_miso,
    .mosi = &gpio_spi_mosi,
    .sck = &gpio_spi_sck,
    .cs = &gpio_sdcard_cs,
};

const FuriHalSpiBusHandle furi_hal_spi_bus_handle_sd_slow = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_sd_slow_event_callback,
    .miso = &gpio_spi_miso,
    .mosi = &gpio_spi_mosi,
    .sck = &gpio_spi_sck,
    .cs = &gpio_sdcard_cs,
};

const FuriHalSpiBusHandle furi_hal_spi_bus_handle_subghz = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_subghz_event_callback,
    .miso = &gpio_spi_miso,
    .mosi = &gpio_spi_mosi,
    .sck = &gpio_spi_sck,
    .cs = &gpio_subghz_cs,
};

const FuriHalSpiBusHandle furi_hal_spi_bus_handle_nfc = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_nfc_wrapper_event_callback,
    .miso = &gpio_spi_miso,
    .mosi = &gpio_spi_mosi,
    .sck = &gpio_spi_sck,
    .cs = &gpio_nfc_cs,
};

// +++++++++ MOVED BLOCK START +++++++++
// (Moved from above for better organization)
const FuriHalSpiBusHandle furi_hal_spi_bus_handle_button_sr = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_button_sr_event_callback,
    .miso = &gpio_spi_miso_BTN,
    .mosi = &gpio_spi_mosi,
    .sck =  &gpio_spi_sck,
    .cs = &gpio_button_sr_latch,
};


// External Handles reinstated - pointing to shared SPI pins
const FuriHalSpiBusHandle furi_hal_spi_bus_handle_external = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_external_wrapper_event_callback, // Use wrapper
    .miso = &gpio_spi_miso, // Use shared pin
    .mosi = &gpio_spi_mosi, // Use shared pin
    .sck = &gpio_spi_sck, // Use shared pin
    .cs = &gpio_ext_pa4, // Ensure this pin is defined and unique
};


const FuriHalSpiBusHandle furi_hal_spi_bus_handle_external_extra = {
    .bus = &furi_hal_spi_bus,
    .callback = furi_hal_spi_bus_handle_external_wrapper_event_callback, // Use wrapper
    .miso = &gpio_spi_miso, // Use shared pin
    .mosi = &gpio_spi_mosi, // Use shared pin
    .sck = &gpio_spi_sck, // Use shared pin
    .cs = &gpio_ext_pc1, // Ensure this pin is defined and unique
};
