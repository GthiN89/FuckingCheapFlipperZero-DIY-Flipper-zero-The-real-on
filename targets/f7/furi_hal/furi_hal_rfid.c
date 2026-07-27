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

// Optimized ring buffer to fit within strict BSS RAM constraints
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

// Stores the HIGH phase width until the rising edge completes the period
static volatile uint32_t saved_pulse = 0;

FuriHalRfidCompCallback furi_hal_rfid_comp_callback = NULL;
void* furi_hal_rfid_comp_callback_context = NULL;

/**
 * Timer callback - dumps logs from buffer (only prints durations)
 */
static void log_timer_callback(void* context) {
    UNUSED(context);
    
    // Dump all data from the buffer
    while(log_buffer.tail != log_buffer.head) {
        uint32_t duration = log_buffer.duration[log_buffer.tail];
        log_buffer.tail = (log_buffer.tail + 1) % LOG_BUFFER_SIZE;
        log_buffer.count--;
        
        // Output only the precise time durations to CLI
        FURI_LOG_I(TAG, "%lu", duration);
    }
}

/**
 * PA2 GPIO Edge Interrupt - Measures raw periods and corrects polarity
 */
static void rfid_pa2_edge_isr(void* context) {
    UNUSED(context);
    
    isr_calls++;
    
    if(!furi_hal_rfid || !furi_hal_rfid->is_capturing) {
        return;
    }
    
    uint32_t elapsed = LL_TIM_GetCounter(RFID_CAPTURE_TIM);
    bool level = furi_hal_gpio_read(&rfid_ext_pin);
    
    // Invert the level if your demodulator outputs active-low signal
    level = !level;
    
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

    // Initialize log buffer
    log_buffer.head = 0;
    log_buffer.tail = 0;
    log_buffer.count = 0;

    // Create timer for dumping logs (50ms interval)
    log_timer = furi_timer_alloc(log_timer_callback, FuriTimerTypePeriodic, NULL);
    furi_timer_start(log_timer, 50);

    furi_hal_rfid_pins_reset();
    
    FURI_LOG_I(TAG, "RFID HAL INIT COMPLETE");
}

void furi_hal_rfid_pins_reset(void) {
    // ibutton bus disable
    furi_hal_ibutton_pin_reset();

    // CL (Carrier) pin reset low
    furi_hal_gpio_init(&gpio_rfid_carrier_out, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(&gpio_rfid_carrier_out, false);

    // DAT pin reset
    furi_hal_gpio_init(&gpio_rfid_data_in, GpioModeInput, GpioPullNo, GpioSpeedLow);
    
    // PA2 reset - remove interrupt
    furi_hal_gpio_remove_int_callback(&rfid_ext_pin);
    furi_hal_gpio_init(&rfid_ext_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);
    
    // Unused pins reset
    furi_hal_gpio_init(&gpio_nfc_irq_rfid_pull, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init_simple(&gpio_rfid_carrier, GpioModeAnalog);
}

static void furi_hal_rfid_pins_emulate(void) {
    // ibutton low
    furi_hal_ibutton_pin_configure();
    furi_hal_ibutton_pin_write(false);

    // В простой схеме эмуляция делается модуляцией сигнала на пине CL.
    // PA7 поддерживает TIM2_CH3 на AF2.
    // Переключаем PA7 (CL) на TIM2 для эмуляции.
    furi_hal_gpio_init_ex(
        &gpio_rfid_carrier_out, // PA7 (CL)
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFn2TIM2); // AF2 is TIM2 on STM32WB55 for PA7
}

static void furi_hal_rfid_pins_read(void) {
    // ibutton low
    furi_hal_ibutton_pin_configure();
    furi_hal_ibutton_pin_write(false);

    // CL (Carrier) pin to TIM1 (125kHz Generator)
    furi_hal_gpio_init_ex(
        &gpio_rfid_carrier_out,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedLow,
        GpioAltFn1TIM1);

    // PA2 as GPIO input with interrupts - BOTH EDGES
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

static void furi_hal_rfid_tim_emulate(void) {
    LL_TIM_SetPrescaler(FURI_HAL_RFID_EMULATE_TIMER, 0);
    LL_TIM_SetCounterMode(FURI_HAL_RFID_EMULATE_TIMER, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetAutoReload(FURI_HAL_RFID_EMULATE_TIMER, 1);
    LL_TIM_DisableARRPreload(FURI_HAL_RFID_EMULATE_TIMER);
    LL_TIM_SetRepetitionCounter(FURI_HAL_RFID_EMULATE_TIMER, 0);

    LL_TIM_SetClockDivision(FURI_HAL_RFID_EMULATE_TIMER, LL_TIM_CLOCKDIVISION_DIV1);
    LL_TIM_SetClockSource(FURI_HAL_RFID_EMULATE_TIMER, LL_TIM_CLOCKSOURCE_EXT_MODE2);
    LL_TIM_ConfigETR(
        FURI_HAL_RFID_EMULATE_TIMER,
        LL_TIM_ETR_POLARITY_INVERTED,
        LL_TIM_ETR_PRESCALER_DIV1,
        LL_TIM_ETR_FILTER_FDIV1);

    LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};
    TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_PWM1;
    TIM_OC_InitStruct.OCState = LL_TIM_OCSTATE_ENABLE;
    TIM_OC_InitStruct.CompareValue = 1;
    
    // Используем Channel 3, который выходит на PA7 (AF2)
    LL_TIM_OC_Init(
        FURI_HAL_RFID_EMULATE_TIMER, FURI_HAL_RFID_EMULATE_TIMER_CHANNEL, &TIM_OC_InitStruct);

    LL_TIM_GenerateEvent_UPDATE(FURI_HAL_RFID_EMULATE_TIMER);
}


void furi_hal_rfid_tim_read_capture_start(FuriHalRfidReadCaptureCallback callback, void* context) {
    furi_check(furi_hal_rfid);

    furi_hal_rfid->read_capture_callback = callback;
    furi_hal_rfid->context = context;
    furi_hal_rfid->is_capturing = true;

    isr_calls = 0;
    edge_count = 0;
    last_duration = 0;
    
    // Reset log buffer
    log_buffer.head = 0;
    log_buffer.tail = 0;
    log_buffer.count = 0;

    // Start the microsecond stopwatch (TIM2)
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
    
    // Wait a bit for timer to dump remaining logs
    furi_delay_ms(100);
}

static void furi_hal_rfid_dma_isr(void* context) {
    UNUSED(context);
#if RFID_DMA_CH1_CHANNEL == LL_DMA_CHANNEL_1
    if(LL_DMA_IsActiveFlag_HT1(RFID_DMA)) {
        LL_DMA_ClearFlag_HT1(RFID_DMA);
        furi_hal_rfid->dma_callback(true, furi_hal_rfid->context);
    }
    if(LL_DMA_IsActiveFlag_TC1(RFID_DMA)) {
        LL_DMA_ClearFlag_TC1(RFID_DMA);
        furi_hal_rfid->dma_callback(false, furi_hal_rfid->context);
    }
#else
#error Update this code. Would you kindly?
#endif
}

void furi_hal_rfid_tim_emulate_dma_start(
    uint32_t* duration,
    uint32_t* pulse,
    size_t length,
    FuriHalRfidDMACallback callback,
    void* context) {
    furi_check(furi_hal_rfid);

    // setup interrupts
    furi_hal_rfid->dma_callback = callback;
    furi_hal_rfid->context = context;

    // setup pins
    furi_hal_rfid_pins_emulate();

    // configure timer
    furi_hal_bus_enable(FURI_HAL_RFID_EMULATE_TIMER_BUS);
    furi_hal_rfid_tim_emulate();
    LL_TIM_OC_SetPolarity(
        FURI_HAL_RFID_EMULATE_TIMER, FURI_HAL_RFID_EMULATE_TIMER_CHANNEL, LL_TIM_OCPOLARITY_HIGH);
    LL_TIM_EnableDMAReq_UPDATE(FURI_HAL_RFID_EMULATE_TIMER);

    // configure DMA "mem -> ARR" channel
    LL_DMA_InitTypeDef dma_config = {0};
    dma_config.PeriphOrM2MSrcAddress = (uint32_t) & (FURI_HAL_RFID_EMULATE_TIMER->ARR);
    dma_config.MemoryOrM2MDstAddress = (uint32_t)duration;
    dma_config.Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    dma_config.Mode = LL_DMA_MODE_CIRCULAR;
    dma_config.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
    dma_config.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
    dma_config.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_WORD;
    dma_config.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_WORD;
    dma_config.NbData = length;
    dma_config.PeriphRequest = LL_DMAMUX_REQ_TIM2_UP;
    dma_config.Priority = LL_DMA_MODE_NORMAL;
    LL_DMA_Init(RFID_DMA_CH1_DEF, &dma_config);
    LL_DMA_EnableChannel(RFID_DMA_CH1_DEF);

    // configure DMA "mem -> CCR3" channel
#if FURI_HAL_RFID_EMULATE_TIMER_CHANNEL == LL_TIM_CHANNEL_CH3
    // Correct for PA7
    dma_config.PeriphOrM2MSrcAddress = (uint32_t) & (FURI_HAL_RFID_EMULATE_TIMER->CCR3);
#else
#error Update this code. Would you kindly?
#endif
    dma_config.MemoryOrM2MDstAddress = (uint32_t)pulse;
    dma_config.Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    dma_config.Mode = LL_DMA_MODE_CIRCULAR;
    dma_config.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
    dma_config.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
    dma_config.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_WORD;
    dma_config.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_WORD;
    dma_config.NbData = length;
    dma_config.PeriphRequest = LL_DMAMUX_REQ_TIM2_UP;
    dma_config.Priority = LL_DMA_MODE_NORMAL;
    LL_DMA_Init(RFID_DMA_CH2_DEF, &dma_config);
    LL_DMA_EnableChannel(RFID_DMA_CH2_DEF);

    // attach interrupt to one of DMA channels
    furi_hal_interrupt_set_isr(RFID_DMA_CH1_IRQ, furi_hal_rfid_dma_isr, NULL);
    LL_DMA_EnableIT_TC(RFID_DMA_CH1_DEF);
    LL_DMA_EnableIT_HT(RFID_DMA_CH1_DEF);

    // start
    LL_TIM_EnableAllOutputs(FURI_HAL_RFID_EMULATE_TIMER);

    LL_TIM_SetCounter(FURI_HAL_RFID_EMULATE_TIMER, 0);
    LL_TIM_EnableCounter(FURI_HAL_RFID_EMULATE_TIMER);
}

void furi_hal_rfid_tim_emulate_dma_stop(void) {
    LL_TIM_DisableCounter(FURI_HAL_RFID_EMULATE_TIMER);
    LL_TIM_DisableAllOutputs(FURI_HAL_RFID_EMULATE_TIMER);

    furi_hal_interrupt_set_isr(RFID_DMA_CH1_IRQ, NULL, NULL);
    LL_DMA_DisableIT_TC(RFID_DMA_CH1_DEF);
    LL_DMA_DisableIT_HT(RFID_DMA_CH1_DEF);

    FURI_CRITICAL_ENTER();

    LL_DMA_DeInit(RFID_DMA_CH1_DEF);
    LL_DMA_DeInit(RFID_DMA_CH2_DEF);

    furi_hal_bus_disable(FURI_HAL_RFID_EMULATE_TIMER_BUS);

    FURI_CRITICAL_EXIT();
}

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

void furi_hal_rfid_comp_start(void) {
    furi_check(furi_hal_rfid);
    FURI_LOG_I(TAG, "COMP Start: routing read edges on PA2 EXTI using TIM2 Stopwatch");

    furi_hal_rfid->is_capturing = true;

    isr_calls = 0;
    edge_count = 0;
    last_duration = 0;
    
    // Reset log buffer
    log_buffer.head = 0;
    log_buffer.tail = 0;
    log_buffer.count = 0;

    // Start the microsecond stopwatch (TIM2) using the structured API pattern
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

    // PA2 as GPIO input with interrupts - BOTH EDGES
    furi_hal_gpio_init(
        &rfid_ext_pin,
        GpioModeInterruptRiseFall,
        GpioPullNo,
        GpioSpeedVeryHigh);

    furi_hal_gpio_add_int_callback(&rfid_ext_pin, rfid_pa2_edge_isr, NULL);
}

void furi_hal_rfid_comp_stop(void) {
    FURI_LOG_I(TAG, "COMP Stop: disabling PA2 EXTI and stopping TIM2");
    furi_hal_rfid->is_capturing = false;
    
    furi_hal_gpio_remove_int_callback(&rfid_ext_pin);
    LL_TIM_DisableCounter(RFID_CAPTURE_TIM);
    furi_hal_bus_disable(RFID_CAPTURE_TIM_BUS);
    
    // Reset pin to safe low-power state
    furi_hal_gpio_init(&rfid_ext_pin, GpioModeInput, GpioPullNo, GpioSpeedLow);
    
    // Wait a bit for log timer to flush
    furi_delay_ms(100);
}

void furi_hal_rfid_comp_set_callback(FuriHalRfidCompCallback cb, void* ctx) {
    FURI_CRITICAL_ENTER();
    furi_hal_rfid_comp_callback = cb;
    furi_hal_rfid_comp_callback_context = ctx;
    __DMB();
    FURI_CRITICAL_EXIT();
}

// Stubs
void COMP_IRQHandler(void) {}
void furi_hal_rfid_field_detect_start(void) {}
void furi_hal_rfid_field_detect_stop(void) {}
bool furi_hal_rfid_field_is_present(uint32_t* f) { 
    if(f) *f = 125000; 
    return false; 
}