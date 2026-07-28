#include <furi_hal_rfid.h>
#include <furi_hal_ibutton.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>
#include <furi_hal_bus.h>
#include <furi.h>

#include <stm32wbxx_ll_tim.h>
#include <stm32wbxx_ll_dma.h>

#define TAG "RFID_DIY"
#define LOG_BUFFER_SIZE 256

#define FURI_HAL_RFID_READ_TIMER                TIM1
#define FURI_HAL_RFID_READ_TIMER_BUS            FuriHalBusTIM1
#define FURI_HAL_RFID_READ_TIMER_CHANNEL        LL_TIM_CHANNEL_CH1N
#define FURI_HAL_RFID_READ_TIMER_CHANNEL_CONFIG LL_TIM_CHANNEL_CH1

#define FURI_HAL_RFID_EMULATE_TIMER         TIM2
#define FURI_HAL_RFID_EMULATE_TIMER_BUS     FuriHalBusTIM2
#define FURI_HAL_RFID_EMULATE_TIMER_IRQ     FuriHalInterruptIdTIM2
#define FURI_HAL_RFID_EMULATE_TIMER_CHANNEL LL_TIM_CHANNEL_CH3

#define RFID_CAPTURE_TIM     TIM2
#define RFID_CAPTURE_TIM_BUS FuriHalBusTIM2

// DIY Hardware Pins
static const GpioPin rfid_ext_pin = {.port = GPIOA, .pin = LL_GPIO_PIN_2};

/* DMA Channels definition */
#define RFID_DMA             DMA2
#define RFID_DMA_CH1_CHANNEL LL_DMA_CHANNEL_1
#define RFID_DMA_CH2_CHANNEL LL_DMA_CHANNEL_2
#define RFID_DMA_CH1_IRQ     FuriHalInterruptIdDma2Ch1
#define RFID_DMA_CH1_DEF     RFID_DMA, RFID_DMA_CH1_CHANNEL
#define RFID_DMA_CH2_DEF     RFID_DMA, RFID_DMA_CH2_CHANNEL

// Optimized ring buffer
typedef struct {
    uint8_t level[LOG_BUFFER_SIZE];
    uint32_t duration[LOG_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
} LogBuffer;

static LogBuffer log_buffer = {0};
static FuriTimer* log_timer = NULL;

typedef struct {
    FuriHalRfidDMACallback dma_callback;
    FuriHalRfidReadCaptureCallback read_capture_callback;
    void* context;
    bool is_capturing;
} FuriHalRfid;

FuriHalRfid* furi_hal_rfid = NULL;

static volatile uint32_t isr_calls = 0;
static volatile uint32_t edge_count = 0;
static volatile uint32_t last_duration = 0;
static volatile bool last_level = false;
static volatile uint32_t saved_pulse = 0;

FuriHalRfidCompCallback furi_hal_rfid_comp_callback = NULL;
void* furi_hal_rfid_comp_callback_context = NULL;

/**
 * Timer callback - dumps logs from buffer
 */
static void log_timer_callback(void* context) {
    UNUSED(context);
    while(log_buffer.tail != log_buffer.head) {
        uint32_t duration = log_buffer.duration[log_buffer.tail];
        log_buffer.tail = (log_buffer.tail + 1) % LOG_BUFFER_SIZE;
        log_buffer.count--;
        FURI_LOG_I(TAG, "%lu", duration);
    }
}

/**
 * PA2 GPIO Edge Interrupt - Measures raw periods and corrects polarity
 */
static void rfid_pa2_edge_isr(void* context) {
    UNUSED(context);

    isr_calls++;
    if(!furi_hal_rfid || !furi_hal_rfid->is_capturing) return;

    uint32_t elapsed = LL_TIM_GetCounter(RFID_CAPTURE_TIM);
    bool level = furi_hal_gpio_read(&rfid_ext_pin);
    level = !level; // Invert if active-low

    if(level) {
        uint32_t duration = elapsed;
        if(duration <= 100) return;
        LL_TIM_SetCounter(RFID_CAPTURE_TIM, 0);

        last_level = false;
        last_duration = duration;
        edge_count++;

        uint32_t next_head = (log_buffer.head + 1) % LOG_BUFFER_SIZE;
        if(next_head != log_buffer.tail) {
            log_buffer.level[log_buffer.head] = last_level;
            log_buffer.duration[log_buffer.head] = last_duration;
            log_buffer.head = next_head;
            log_buffer.count++;
        }

        if(furi_hal_rfid->read_capture_callback) {
            furi_hal_rfid->read_capture_callback(false, last_duration, furi_hal_rfid->context);
        }
        if(furi_hal_rfid_comp_callback) {
            furi_hal_rfid_comp_callback(false, furi_hal_rfid_comp_callback_context);
        }
    } else {
        uint32_t pulse = elapsed;
        if(pulse <= 100) return;
        saved_pulse = pulse;

        if(furi_hal_rfid->read_capture_callback) {
            furi_hal_rfid->read_capture_callback(true, pulse, furi_hal_rfid->context);
        }
        if(furi_hal_rfid_comp_callback) {
            furi_hal_rfid_comp_callback(true, furi_hal_rfid_comp_callback_context);
        }
    }
}

void furi_hal_rfid_init(void) {
    furi_check(furi_hal_rfid == NULL);
    furi_hal_rfid = malloc(sizeof(FuriHalRfid));
    furi_hal_rfid->is_capturing = false;
    furi_hal_rfid->read_capture_callback = NULL;
    furi_hal_rfid->context = NULL;

    log_buffer.head = 0;
    log_buffer.tail = 0;
    log_buffer.count = 0;

    log_timer = furi_timer_alloc(log_timer_callback, FuriTimerTypePeriodic, NULL);
    furi_timer_start(log_timer, 50);

    furi_hal_rfid_pins_reset();
    FURI_LOG_I(TAG, "RFID HAL INIT COMPLETE");
}

void furi_hal_rfid_pins_reset(void) {
    furi_hal_ibutton_pin_reset();

    furi_hal_gpio_init(&gpio_rfid_carrier_out, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(&gpio_rfid_carrier_out, false);

    furi_hal_gpio_init(&gpio_rfid_data_in, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_remove_int_callback(&rfid_ext_pin);
    furi_hal_gpio_init(&rfid_ext_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);

    furi_hal_gpio_init(&gpio_nfc_irq_rfid_pull, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init_simple(&gpio_rfid_carrier, GpioModeAnalog);
}

static void furi_hal_rfid_pins_read(void) {
    furi_hal_ibutton_pin_configure();
    furi_hal_ibutton_pin_write(false);

    furi_hal_gpio_init_ex(
        &gpio_rfid_carrier_out,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFn1TIM1);

    furi_hal_gpio_init(
        &rfid_ext_pin,
        GpioModeInterruptRiseFall,
        GpioPullNo,
        GpioSpeedVeryHigh);
}

void furi_hal_rfid_pin_pull_release(void) {}
void furi_hal_rfid_pin_pull_pulldown(void) {}

void furi_hal_rfid_tim_read_start(float freq, float duty_cycle) {
    furi_hal_bus_enable(FURI_HAL_RFID_READ_TIMER_BUS);
    furi_hal_rfid_pins_read();

    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Autoreload = (SystemCoreClock / freq) - 1;
    LL_TIM_Init(FURI_HAL_RFID_READ_TIMER, &TIM_InitStruct);
    LL_TIM_DisableARRPreload(FURI_HAL_RFID_READ_TIMER);

    LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};
    TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_PWM1;
    TIM_OC_InitStruct.OCNState = LL_TIM_OCSTATE_ENABLE;
    TIM_OC_InitStruct.CompareValue = TIM_InitStruct.Autoreload * duty_cycle;
    LL_TIM_OC_Init(
        FURI_HAL_RFID_READ_TIMER, FURI_HAL_RFID_READ_TIMER_CHANNEL_CONFIG, &TIM_OC_InitStruct);

    LL_TIM_EnableCounter(FURI_HAL_RFID_READ_TIMER);
    furi_hal_rfid_tim_read_continue();
}

void furi_hal_rfid_tim_read_continue(void) {
    LL_TIM_EnableAllOutputs(FURI_HAL_RFID_READ_TIMER);
}

void furi_hal_rfid_tim_read_pause(void) {
    LL_TIM_DisableAllOutputs(FURI_HAL_RFID_READ_TIMER);
}

void furi_hal_rfid_tim_read_stop(void) {
    furi_hal_bus_disable(FURI_HAL_RFID_READ_TIMER_BUS);
}

// ---------------------------------------------------------------------
// Software Emulation (pauses/resumes TIM1 carrier)
// ---------------------------------------------------------------------

static volatile bool emulation_stop_requested = false;

void furi_hal_rfid_tim_emulate_dma_start(
    uint32_t* duration,
    uint32_t* pulse,
    size_t length,
    FuriHalRfidDMACallback callback,
    void* context) {

    furi_check(furi_hal_rfid);
    FURI_LOG_I(TAG, "Starting continuous software emulation (duty-cycle modulation)");

    // Configure PA7 as TIM1_CH1N output (carrier)
    furi_hal_ibutton_pin_configure();
    furi_hal_ibutton_pin_write(false);

    furi_hal_gpio_init_ex(
        &gpio_rfid_carrier_out,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFn1TIM1);

    // Initialise TIM1 for 125 kHz carrier
    furi_hal_bus_enable(FURI_HAL_RFID_READ_TIMER_BUS);

    uint32_t period = (SystemCoreClock / 125000.0f) - 1;
    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Autoreload = period;
    LL_TIM_Init(FURI_HAL_RFID_READ_TIMER, &TIM_InitStruct);
    LL_TIM_DisableARRPreload(FURI_HAL_RFID_READ_TIMER);

    // Configure CH1 (main) with OCNState = ENABLE so CH1N follows CH1
    LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};
    TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_PWM1;
    TIM_OC_InitStruct.OCState = LL_TIM_OCSTATE_ENABLE;      // Enable CH1 (main)
    TIM_OC_InitStruct.OCNState = LL_TIM_OCSTATE_ENABLE;     // Enable CH1N (complement)
    TIM_OC_InitStruct.CompareValue = period / 2;            // 50% duty initially
    LL_TIM_OC_Init(
        FURI_HAL_RFID_READ_TIMER, FURI_HAL_RFID_READ_TIMER_CHANNEL_CONFIG, &TIM_OC_InitStruct);

    // Enable CH1 and start counter
    LL_TIM_CC_EnableChannel(FURI_HAL_RFID_READ_TIMER, LL_TIM_CHANNEL_CH1);
    LL_TIM_EnableCounter(FURI_HAL_RFID_READ_TIMER);
    LL_TIM_EnableAllOutputs(FURI_HAL_RFID_READ_TIMER);   // Now CH1N is active

    // Reset stop flag
    emulation_stop_requested = false;

    // --- Continuous emulation loop ---
    while(!emulation_stop_requested) {
        // Send the whole data packet
        for(size_t i = 0; i < length; i++) {
            // Check for stop request inside the loop (react quickly)
            if(emulation_stop_requested) break;

            // Carrier ON: set compare to 50% duty
            if(pulse[i] > 0) {
                LL_TIM_OC_SetCompareCH1(FURI_HAL_RFID_READ_TIMER, period / 2);
                furi_delay_us(pulse[i]);
            }

            // Carrier OFF: set compare to 0 (0% duty)
            uint32_t off_time = duration[i] - pulse[i];
            if(off_time > 0) {
                LL_TIM_OC_SetCompareCH1(FURI_HAL_RFID_READ_TIMER, 0);
                furi_delay_us(off_time);
            }
        }

    }

    // --- Cleanup on stop ---
    LL_TIM_OC_SetCompareCH1(FURI_HAL_RFID_READ_TIMER, 0);
    LL_TIM_DisableAllOutputs(FURI_HAL_RFID_READ_TIMER);
    LL_TIM_DisableCounter(FURI_HAL_RFID_READ_TIMER);
    furi_hal_bus_disable(FURI_HAL_RFID_READ_TIMER_BUS);

    // Call callback (if provided) to signal completion
    if(callback) {
        callback(false, context);
    }
}

void furi_hal_rfid_tim_emulate_dma_stop(void) {
    FURI_LOG_I(TAG, "Stopping emulation");
    emulation_stop_requested = true;
    // The start function will exit its loop and clean up
}


// ---------------------------------------------------------------------
// Reading Capture
// ---------------------------------------------------------------------

void furi_hal_rfid_tim_read_capture_start(FuriHalRfidReadCaptureCallback callback, void* context) {
    furi_check(furi_hal_rfid);

    furi_hal_rfid->read_capture_callback = callback;
    furi_hal_rfid->context = context;
    furi_hal_rfid->is_capturing = true;

    isr_calls = 0;
    edge_count = 0;
    last_duration = 0;
    log_buffer.head = 0;
    log_buffer.tail = 0;
    log_buffer.count = 0;

    furi_hal_bus_enable(RFID_CAPTURE_TIM_BUS);

    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Prescaler = 127;   // 128 MHz timer input → 1 µs per tick
    TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
    TIM_InitStruct.Autoreload = UINT32_MAX;
    TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(RFID_CAPTURE_TIM, &TIM_InitStruct);

    LL_TIM_SetClockSource(RFID_CAPTURE_TIM, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(RFID_CAPTURE_TIM);
    LL_TIM_SetCounter(RFID_CAPTURE_TIM, 0);
    LL_TIM_EnableCounter(RFID_CAPTURE_TIM);

    FURI_LOG_I(TAG, "=== CAPTURE STARTED ===");
    FURI_LOG_I(TAG, "Callback: %p", callback);
    FURI_LOG_I(TAG, "Waiting for edges...");

    furi_hal_gpio_add_int_callback(&rfid_ext_pin, rfid_pa2_edge_isr, NULL);
}

void furi_hal_rfid_tim_read_capture_stop(void) {
    FURI_LOG_I(TAG, "=== CAPTURE STOPPED ===");
    FURI_LOG_I(TAG, "Total ISR calls: %lu", isr_calls);
    FURI_LOG_I(TAG, "Total edges: %lu", edge_count);
    FURI_LOG_I(TAG, "Buffer entries remaining: %lu", log_buffer.count);

    furi_hal_rfid->is_capturing = false;
    furi_hal_gpio_remove_int_callback(&rfid_ext_pin);
    LL_TIM_DisableCounter(RFID_CAPTURE_TIM);
    furi_hal_bus_disable(RFID_CAPTURE_TIM_BUS);
    furi_hal_rfid->read_capture_callback = NULL;
    furi_hal_rfid->context = NULL;
    furi_delay_ms(100);
}

// ---------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------

void furi_hal_rfid_set_read_period(uint32_t period) {
    LL_TIM_SetAutoReload(FURI_HAL_RFID_READ_TIMER, period);
}

void furi_hal_rfid_set_read_pulse(uint32_t pulse) {
#if FURI_HAL_RFID_READ_TIMER_CHANNEL == LL_TIM_CHANNEL_CH1N
    LL_TIM_OC_SetCompareCH1(FURI_HAL_RFID_READ_TIMER, pulse);
#else
#error Update this code. Would you kindly?
#endif
}

// ---------------------------------------------------------------------
// Comparator Stubs (no longer used)
// ---------------------------------------------------------------------

void furi_hal_rfid_comp_start(void) {
    furi_check(furi_hal_rfid);
    FURI_LOG_I(TAG, "COMP Start: routing read edges on PA2 EXTI using TIM2 Stopwatch");

    furi_hal_rfid->is_capturing = true;
    isr_calls = 0;
    edge_count = 0;
    last_duration = 0;
    log_buffer.head = 0;
    log_buffer.tail = 0;
    log_buffer.count = 0;

    furi_hal_bus_enable(RFID_CAPTURE_TIM_BUS);

    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Prescaler = 127;
    TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
    TIM_InitStruct.Autoreload = UINT32_MAX;
    TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(RFID_CAPTURE_TIM, &TIM_InitStruct);

    LL_TIM_SetClockSource(RFID_CAPTURE_TIM, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(RFID_CAPTURE_TIM);
    LL_TIM_SetCounter(RFID_CAPTURE_TIM, 0);
    LL_TIM_EnableCounter(RFID_CAPTURE_TIM);

    furi_hal_gpio_init(&rfid_ext_pin, GpioModeInterruptRiseFall, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_add_int_callback(&rfid_ext_pin, rfid_pa2_edge_isr, NULL);
}

void furi_hal_rfid_comp_stop(void) {
    FURI_LOG_I(TAG, "COMP Stop: disabling PA2 EXTI and stopping TIM2");
    furi_hal_rfid->is_capturing = false;
    furi_hal_gpio_remove_int_callback(&rfid_ext_pin);
    LL_TIM_DisableCounter(RFID_CAPTURE_TIM);
    furi_hal_bus_disable(RFID_CAPTURE_TIM_BUS);
    furi_hal_gpio_init(&rfid_ext_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_delay_ms(100);
}

void furi_hal_rfid_comp_set_callback(FuriHalRfidCompCallback cb, void* ctx) {
    FURI_CRITICAL_ENTER();
    furi_hal_rfid_comp_callback = cb;
    furi_hal_rfid_comp_callback_context = ctx;
    __DMB();
    FURI_CRITICAL_EXIT();
}

// ---------------------------------------------------------------------
// Field Detection Stubs
// ---------------------------------------------------------------------

void COMP_IRQHandler(void) {}
void furi_hal_rfid_field_detect_start(void) {}
void furi_hal_rfid_field_detect_stop(void) {}
bool furi_hal_rfid_field_is_present(uint32_t* f) {
    if(f) *f = 125000;
    return false;
}