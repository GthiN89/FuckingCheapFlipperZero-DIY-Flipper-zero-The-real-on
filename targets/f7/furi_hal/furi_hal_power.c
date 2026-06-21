#include <furi_hal_power.h>
#include <furi.h>
#include <stdbool.h>
#include <stdint.h>
#include <furi_hal_adc.h>
#include <furi_hal_ina219.h>
#include <string.h>

#define TAG "FuriHalPower"

// --- Constants ---
#define BATTERY_CAPACITY (800)    // mAh
#define V_MIN (3.00f)              // 0% voltage
#define V_MAX (4.20f)              // 100% voltage
#define R_INTERNAL (0.1f)          // Shunt resistor: 0.1Ω
#define CHARGING_CURRENT_SIGN (-1.0f) // -1.0: negative current = charging

// --- State ---
typedef struct {
    volatile uint8_t insomnia;
    volatile uint8_t suppress_charge;
    bool gauge_ok;
    bool charger_ok;
} FuriHalPower;

static volatile FuriHalPower furi_hal_power = {
    .insomnia = 0,
    .suppress_charge = 0,
    .gauge_ok = false,
    .charger_ok = false,
};

#ifdef USE_INA219
static float curr_soc_percent = 100.0f;
static bool prev_charging_status = false;
static uint8_t prev_battery_pct = 0;
#endif

// --- Helpers ---
static float clamp(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static uint8_t get_pct_adc_fallback(void);

// --- Public API ---

void furi_hal_power_init(void) {
    furi_hal_adc_init();
#ifdef USE_INA219
    FURI_LOG_I(TAG, "Initializing INA219 power sensor");
    furi_hal_ina219_init();
#else
    FURI_LOG_I(TAG, "INA219 support not enabled at build time");
#endif
}

bool furi_hal_power_gauge_is_ok(void) {
    return true;
}

bool furi_hal_power_is_shutdown_requested(void) {
    return false;
}

uint16_t furi_hal_power_insomnia_level(void) {
    return furi_hal_power.insomnia;
}

void furi_hal_power_insomnia_enter(void) {
    FURI_CRITICAL_ENTER();
    furi_check(furi_hal_power.insomnia < UINT8_MAX);
    furi_hal_power.insomnia++;
    FURI_CRITICAL_EXIT();
}

void furi_hal_power_insomnia_exit(void) {
    FURI_CRITICAL_ENTER();
    furi_check(furi_hal_power.insomnia > 0);
    furi_hal_power.insomnia--;
    FURI_CRITICAL_EXIT();
}

bool furi_hal_power_sleep_available(void) {
    return furi_hal_power.insomnia == 0;
}

void furi_hal_power_sleep(void) {
    // Stub
}

uint8_t furi_hal_power_get_pct(void) {
#ifdef USE_INA219
    if (!furi_hal_ina219_is_ready()) {
        return get_pct_adc_fallback();
    }

    float v = 0.0f, i = 0.0f;
    if (!furi_hal_ina219_get_voltage_current(&v, &i)) {
        return get_pct_adc_fallback();
    }

    static float soc_percent = 100.0f;
    static uint32_t last_ms = 0;
    static float smoothed_v = 0.0f;
    static float smoothed_i = 0.0f;
    static uint32_t transient_since_ms = 0;

    // --- Charging detection ---
    bool is_charging = (i * CHARGING_CURRENT_SIGN) < 0.0f;
    if (is_charging != prev_charging_status) {
        FURI_LOG_I(TAG, "Charging status changed: %s (V=%.3fV, I=%.3fA)",
                  is_charging ? "CHARGING" : "NOT CHARGING", (double)v, (double)i);
        prev_charging_status = is_charging;
    }

    // --- Low current charging (taper/float) ---
    if (is_charging && (i * CHARGING_CURRENT_SIGN) > -0.1f && (i * CHARGING_CURRENT_SIGN) < 0.0f) {
        FURI_LOG_D(TAG, "Low current charging: SOC=99%% (I=%.3fA)", (double)i);
        soc_percent = 99.0f;
        curr_soc_percent = 99.0f;
        last_ms = 0;
        uint8_t pct = 99;
        if (pct != prev_battery_pct) {
            FURI_LOG_I(TAG, "Battery percentage changed: %u%%", pct);
            prev_battery_pct = pct;
        }
        return pct;
    }

    // --- Open-circuit voltage estimation ---
    float v_opencircuit = v - (i * R_INTERNAL);
    v_opencircuit = clamp(v_opencircuit, V_MIN, V_MAX);

    // --- Voltage-to-SOC curve (Li-ion) ---
    float v_norm = (v_opencircuit - V_MIN) / (V_MAX - V_MIN);
    float v_soc;
    if (v_norm < 0.1f) {
        v_soc = v_norm * 50.0f;          // 0-5%
    } else if (v_norm < 0.3f) {
        v_soc = 5.0f + (v_norm - 0.1f) * 75.0f; // 5-20%
    } else {
        v_soc = 20.0f + (v_norm - 0.3f) * 114.3f; // 20-100%
    }

    // --- Coulomb counting ---
    uint32_t now = furi_get_tick();
    if (last_ms == 0) {
        last_ms = now;
        soc_percent = v_soc;
        curr_soc_percent = v_soc;
        smoothed_v = v_opencircuit;
        smoothed_i = i;
        transient_since_ms = 0;
    }

    uint32_t dt_ms = now - last_ms;
    last_ms = now;

    if (dt_ms < 100) {
        uint8_t pct = (uint8_t)(curr_soc_percent + 0.5f);
        if (pct != prev_battery_pct) {
            FURI_LOG_I(TAG, "Battery percentage changed: %u%%", pct);
            prev_battery_pct = pct;
        }
        return pct;
    }

    // --- EMA smoothing ---
    const float ALPHA_V_EMA = 0.12f;
    const float ALPHA_I_EMA = 0.12f;
    float prev_smoothed_v = smoothed_v;
    smoothed_v = ALPHA_V_EMA * v_opencircuit + (1.0f - ALPHA_V_EMA) * smoothed_v;
    smoothed_i = ALPHA_I_EMA * i + (1.0f - ALPHA_I_EMA) * smoothed_i;

    // --- Transient detection ---
    if (fabsf(v_opencircuit - prev_smoothed_v) > 0.07f) {
        transient_since_ms = now;
    }
    bool in_transient = (transient_since_ms != 0) && ((now - transient_since_ms) < 8000);

    // --- Capacity change ---
    float delta_mAh = i * (dt_ms / 3600000.0f) * 1000.0f;
    float delta_percent = (delta_mAh / BATTERY_CAPACITY) * 100.0f;
    float coulomb_soc = soc_percent + delta_percent;

    // --- Charging-only mode ---
    if (i * CHARGING_CURRENT_SIGN < -0.01f) { // Charging
        float new_soc = clamp(coulomb_soc, 0.0f, 100.0f);
        float max_delta_per_sec = in_transient ? 0.5f : 2.0f;
        float dt_sec = dt_ms / 1000.0f;
        if (dt_sec <= 0.0f) dt_sec = 1.0f;
        float max_delta = max_delta_per_sec * dt_sec;
        new_soc = clamp(new_soc, curr_soc_percent - max_delta, curr_soc_percent + max_delta);
        soc_percent = new_soc;
        curr_soc_percent = 0.25f * soc_percent + 0.75f * curr_soc_percent;
        curr_soc_percent = clamp(curr_soc_percent, 0.0f, 99.0f);
    }
    // --- Discharging/Idle mode ---
    else {
        // Adaptive blending
        float abs_i = fabsf(i);
        float weight_coulomb = 0.85f;
        if (abs_i < 0.005f) weight_coulomb = 0.25f;
        else if (abs_i < 0.02f) weight_coulomb = 0.5f;
        else if (abs_i < 0.1f) weight_coulomb = 0.75f;

        if (smoothed_v > (V_MAX - 0.03f)) {
            float near_full = (V_MAX - smoothed_v) / 0.03f;
            near_full = clamp(near_full, 0.0f, 1.0f);
            if (abs_i < 0.005f) weight_coulomb *= near_full;
        }

        float weight_voltage = 1.0f - weight_coulomb;

        if (in_transient) {
            weight_voltage = (i * CHARGING_CURRENT_SIGN < -0.01f) ? 0.02f : 0.08f;
            weight_coulomb = 1.0f - weight_voltage;
        }

        if ((i * CHARGING_CURRENT_SIGN < -0.01f) && fabsf(v_opencircuit - prev_smoothed_v) > 0.03f) {
            weight_voltage = fminf(weight_voltage, 0.05f);
            weight_coulomb = 1.0f - weight_voltage;
        }

        float blended = weight_coulomb * coulomb_soc + weight_voltage * v_soc;
        blended = clamp(blended, 0.0f, 100.0f);

        // Rate limiting
        float max_delta_per_sec = in_transient ? 0.5f : 2.0f;
        float dt_sec = dt_ms / 1000.0f;
        if (dt_sec <= 0.0f) dt_sec = 1.0f;
        float max_delta = max_delta_per_sec * dt_sec;
        blended = clamp(blended, curr_soc_percent - max_delta, curr_soc_percent + max_delta);

        // Monotonic enforcement
        if (i * CHARGING_CURRENT_SIGN < -0.01f && blended > curr_soc_percent) {
            blended = curr_soc_percent; // Don't increase during discharge
        } else if (i * CHARGING_CURRENT_SIGN > 0.01f && blended < curr_soc_percent) {
            blended = curr_soc_percent; // Don't decrease during charge
        }

        soc_percent = blended;
        curr_soc_percent = 0.25f * soc_percent + 0.75f * curr_soc_percent;
    }

    // --- Final percentage ---
    uint8_t pct = (uint8_t)(curr_soc_percent + 0.5f);
    if (pct != prev_battery_pct) {
        FURI_LOG_I(TAG, "Battery percentage changed: %u%%", pct);
        prev_battery_pct = pct;
    }

    FURI_LOG_D(TAG, "INA219: V=%.3fV Voc=%.3fV I=%.3fA Vsoc=%.1f%% Csoc=%.1f%% Final=%.1f%%",
              (double)v, (double)v_opencircuit, (double)i, (double)v_soc, (double)coulomb_soc, (double)curr_soc_percent);
    return pct;
#else
    return get_pct_adc_fallback();
#endif
}

// --- ADC Fallback ---
static uint8_t get_pct_adc_fallback(void) {
    FuriHalAdcHandle* handle = furi_hal_adc_acquire();
    if (!handle) return 90;

    furi_hal_adc_configure(handle);
    uint16_t raw_vbat = furi_hal_adc_read(handle, FuriHalAdcChannelVBAT);
    uint16_t raw_vref = furi_hal_adc_read(handle, FuriHalAdcChannelVREFINT);
    float vref_mV = furi_hal_adc_convert_vref(handle, raw_vref);
    float adc_input_mV = ((float)raw_vbat) * vref_mV / 4095.0f;
    float vbat = (adc_input_mV * 3.0f) / 1000.0f;
    furi_hal_adc_release(handle);

    uint8_t pct;
    if (vbat <= V_MIN) pct = 0;
    else if (vbat >= V_MAX) pct = 100;
    else pct = (uint8_t)((vbat - V_MIN) / (V_MAX - V_MIN) * 100.0f + 0.5f);

    if (pct != prev_battery_pct) {
        FURI_LOG_I(TAG, "Battery percentage changed: %u%%", pct);
        prev_battery_pct = pct;
    }
    return pct;
}

// --- Charging Status ---
bool furi_hal_power_is_charging(void) {
#ifdef USE_INA219
    if (furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if (furi_hal_ina219_get_voltage_current(&v, &i)) {
            bool charging = (i * CHARGING_CURRENT_SIGN) < 0.0f;
            if (charging != prev_charging_status) {
                FURI_LOG_I(TAG, "Charging status changed: %s (V=%.3fV, I=%.3fA)",
                          charging ? "CHARGING" : "NOT CHARGING", (double)v, (double)i);
                prev_charging_status = charging;
            }
            return charging;
        }
    }
#endif
    return false;
}

// --- Stub Implementations ---
bool furi_hal_power_is_charging_done(void) { return false; }
void furi_hal_power_shutdown(void) { while (1) {} }
void furi_hal_power_off(void) { furi_hal_power_shutdown(); }
FURI_NORETURN void furi_hal_power_reset(void) { NVIC_SystemReset(); }
bool furi_hal_power_enable_otg(void) { return false; }
void furi_hal_power_disable_otg(void) {}
bool furi_hal_power_is_otg_enabled(void) { return false; }
float furi_hal_power_get_battery_charge_voltage_limit(void) { return 4.2f; }
void furi_hal_power_set_battery_charge_voltage_limit(float voltage) { (void)voltage; }
bool furi_hal_power_check_otg_fault(void) { return false; }
void furi_hal_power_check_otg_status(void) {}
uint32_t furi_hal_power_get_battery_remaining_capacity(void) { return BATTERY_CAPACITY; }
uint32_t furi_hal_power_get_battery_full_capacity(void) { return BATTERY_CAPACITY; }
uint32_t furi_hal_power_get_battery_design_capacity(void) { return BATTERY_CAPACITY; }

float furi_hal_power_get_battery_voltage(FuriHalPowerIC ic) {
    (void)ic;
#ifdef USE_INA219
    if (furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if (furi_hal_ina219_get_voltage_current(&v, &i)) {
            return clamp(v - (i * R_INTERNAL), 0.0f, 4.2f);
        }
    }
#endif
    FuriHalAdcHandle* handle = furi_hal_adc_acquire();
    if (!handle) return 3.7f;
    furi_hal_adc_configure(handle);
    uint16_t raw_vbat = furi_hal_adc_read(handle, FuriHalAdcChannelVBAT);
    uint16_t raw_vref = furi_hal_adc_read(handle, FuriHalAdcChannelVREFINT);
    float vref_mV = furi_hal_adc_convert_vref(handle, raw_vref);
    float adc_input_mV = ((float)raw_vbat) * vref_mV / 4095.0f;
    float vbat = (adc_input_mV * 3.0f) / 1000.0f;
    furi_hal_adc_release(handle);
    return clamp(vbat, 0.0f, 4.2f);
}

float furi_hal_power_get_battery_current(FuriHalPowerIC ic) {
    (void)ic;
#ifdef USE_INA219
    if (furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if (furi_hal_ina219_get_voltage_current(&v, &i)) {
            FURI_LOG_D(TAG, "INA219 voltage=%.3f V, current=%.3f A", (double)v, (double)i);
            return i;
        }
    }
#endif
    return 0.10f;
}

float furi_hal_power_get_battery_temperature(FuriHalPowerIC ic) {
    (void)ic;
    return 25.0f;
}

float furi_hal_power_get_usb_voltage(void) { return 0.0f; }
void furi_hal_power_enable_external_3_3v(void) {}
void furi_hal_power_disable_external_3_3v(void) {}
void furi_hal_power_suppress_charge_enter(void) {}
void furi_hal_power_suppress_charge_exit(void) {}
void furi_hal_power_info_get(PropertyValueCallback out, char sep, void* context) {
    (void)out; (void)sep; (void)context;
}
void furi_hal_power_debug_get(PropertyValueCallback out, void* context) {
    (void)out; (void)context;
}

uint8_t furi_hal_power_get_bat_health_pct(void) {
    // Default: assume 100% health if no gauge is available
    return 100;
}