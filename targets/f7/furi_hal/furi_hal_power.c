#include <furi_hal_power.h>
#include <furi.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include <furi_hal_clock.h>
#include <furi_hal_bt.h>
#include <furi_hal_vibro.h>
#include <furi_hal_resources.h>
#include <furi_hal_adc.h>
#include <furi_hal_serial_control.h>
#include <furi_hal_rtc.h>
#include <furi_hal_debug.h>
#include <stm32wbxx_ll_rcc.h>
#include <stm32wbxx_ll_pwr.h>
#include <stm32wbxx_ll_hsem.h>
#include <stm32wbxx_ll_cortex.h>
#include <stm32wbxx_ll_gpio.h>
#include <hsem_map.h>
#include <bq27220.h>
#include <bq27220_data_memory.h>
#include <bq25896.h>

#ifdef USE_INA219
#include <furi_hal_ina219.h>
#include <string.h>
#endif

#define TAG "FuriHalPower"

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

const int32_t BATTERY_CAPACITY = 800;
#ifdef USE_INA219
float curr_soc_percent = 100.0f;
const float R_INTERNAL = 0.25f;
#endif

void furi_hal_power_init(void) {
#ifdef USE_INA219
    furi_hal_ina219_init();
    return;
#endif
    furi_hal_adc_init();
}

bool furi_hal_power_gauge_is_ok(void) { return true; }
bool furi_hal_power_is_shutdown_requested(void) { return false; }
uint16_t furi_hal_power_insomnia_level(void) { return furi_hal_power.insomnia; }

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

bool furi_hal_power_sleep_available(void) { return furi_hal_power.insomnia == 0; }
void furi_hal_power_sleep(void) {}

uint8_t furi_hal_power_get_pct(void) {
    const float V_MIN = 3.00f; 
    const float V_MAX = 4.20f; 
    static float soc_percent = 100.0f; 
    static uint32_t last_ms = 0;
    static float smoothed_v = 0.0f;
    static float smoothed_i = 0.0f;
    static uint32_t transient_since_ms = 0;
    const float battery_capacity_mAh = BATTERY_CAPACITY; 

#ifdef USE_INA219
    if(furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if(furi_hal_ina219_get_voltage_current(&v, &i)) {
            
            // Minimal memory logic to catch charger V/I spikes
            static float v_jump = 0.0f;
            static float old_i = 0.0f;
            static float old_v = 0.0f;
            static bool initialized = false;

            if(!initialized) {
                old_i = i;
                old_v = v;
                initialized = true;
            } else {
                float di = i - old_i;
                float dv = v - old_v;
                
                // Detect charger connect (sudden current & voltage increase)
                if(di > 0.1f && i > 0.05f) {
                    float unacc_v = dv - (di * R_INTERNAL);
                    if(unacc_v > 0.0f) {
                        v_jump += unacc_v;
                        FURI_LOG_I(TAG, "Plug dI:%.2f dV:%.2f j:%.2f", (double)di, (double)dv, (double)v_jump);
                    }
                } else if(i < 0.05f) {
                    v_jump = 0.0f; // Reset offset when unplugged/not charging
                }
                
                old_i = i;
                old_v = v;
            }

            // Subtract recorded jump from Open Circuit estimate to prevent false instant-fill
            float v_opencircuit = v - (i * R_INTERNAL) - v_jump;
            
            float v_clamped = v_opencircuit;
            if(v_clamped < V_MIN) v_clamped = V_MIN;
            if(v_clamped > V_MAX) v_clamped = V_MAX;
            
            float v_norm = (v_clamped - V_MIN) / (V_MAX - V_MIN);
            float v_soc = 0.0f;
            if(v_norm < 0.1f) {
                v_soc = v_norm * 50.0f; 
            } else if(v_norm < 0.3f) {
                v_soc = 5.0f + (v_norm - 0.1f) * 75.0f; 
            } else {
                v_soc = 20.0f + (v_norm - 0.3f) * 114.3f; 
            }
            
            uint32_t now = furi_get_tick();
            if(last_ms == 0) {
                last_ms = now;
                soc_percent = v_soc;
                curr_soc_percent = v_soc;
                smoothed_v = v_opencircuit;
                smoothed_i = i;
                transient_since_ms = 0;
            }
            uint32_t dt_ms = now - last_ms;
            last_ms = now;

            const float ALPHA_V_EMA = 0.12f;
            const float ALPHA_I_EMA = 0.12f;
            float prev_smoothed_v = smoothed_v;
            smoothed_v = (ALPHA_V_EMA * v_opencircuit) + ((1.0f - ALPHA_V_EMA) * smoothed_v);
            smoothed_i = (ALPHA_I_EMA * i) + ((1.0f - ALPHA_I_EMA) * smoothed_i);

            if(fabsf(v_opencircuit - prev_smoothed_v) > 0.07f) {
                transient_since_ms = now;
            }

            bool in_transient = false;
            const uint32_t TRANSIENT_MS = 8000;
            if(transient_since_ms != 0 && (now - transient_since_ms) < TRANSIENT_MS) {
                in_transient = true;
            }
            
            if(dt_ms < 100) {
                return (uint8_t)(curr_soc_percent + 0.5f);
            }
            
            float delta_mAh = i * ((float)dt_ms / 3600000.0f) * 1000.0f;
            float delta_percent = (delta_mAh / battery_capacity_mAh) * 100.0f;
            float coulomb_soc = soc_percent + delta_percent;

            if(i > 0.01f) {
                float new_soc = coulomb_soc;
                if(new_soc < 0.0f) new_soc = 0.0f;
                if(new_soc > 100.0f) new_soc = 100.0f;

                float max_delta_per_sec = 2.0f; 
                if(in_transient) max_delta_per_sec = 0.5f;
                float dt_sec_local = (float)dt_ms / 1000.0f;
                if(dt_sec_local <= 0.0f) dt_sec_local = 1.0f;
                float max_delta = max_delta_per_sec * dt_sec_local;
                float delta_local = new_soc - curr_soc_percent;
                if(delta_local > max_delta) new_soc = curr_soc_percent + max_delta;
                else if(delta_local < -max_delta) new_soc = curr_soc_percent - max_delta;

                soc_percent = new_soc;

                const float ALPHA_LOCAL = 0.25f;
                curr_soc_percent = (ALPHA_LOCAL * soc_percent) + ((1.0f - ALPHA_LOCAL) * curr_soc_percent);

                curr_soc_percent = (uint8_t)(curr_soc_percent + 0.5f);
                if(curr_soc_percent > 99.0f) curr_soc_percent = 99.0f;
                
                // Minimal Periodic Log for Charging
                static uint32_t last_log = 0;
                if (now - last_log > 3000) {
                    FURI_LOG_I(TAG, "CHG V:%.2f I:%.2f j:%.2f SOC:%d", (double)v, (double)i, (double)v_jump, (int)curr_soc_percent);
                    last_log = now;
                }
                
                return curr_soc_percent;
            }
            
            float abs_i = (i < 0.0f) ? -i : i;
            float battery_full = (float)furi_hal_power_get_battery_full_capacity();
            if(battery_full <= 0.0f) battery_full = battery_capacity_mAh;

            float weight_coulomb = 0.85f; 
            if(abs_i < 0.005f) { 
                weight_coulomb = 0.25f;
            } else if(abs_i < 0.02f) { 
                weight_coulomb = 0.5f;
            } else if(abs_i < 0.1f) { 
                weight_coulomb = 0.75f;
            }

            float v_taper_check = smoothed_v;
            if(v_taper_check > (V_MAX - 0.03f)) { 
                float near_full = (V_MAX - v_taper_check) / 0.03f;
                if(near_full < 0.0f) near_full = 0.0f;
                if(near_full > 1.0f) near_full = 1.0f;
                if(abs_i < 0.005f) {
                    weight_coulomb *= near_full;
                }
            }

            float weight_voltage = 1.0f - weight_coulomb;

            if(in_transient) {
                if(i > 0.01f) {
                    weight_voltage = 0.02f;
                } else {
                    weight_voltage = 0.08f;
                }
                weight_coulomb = 1.0f - weight_voltage;
            }

            if(i > 0.01f && fabsf(v_opencircuit - prev_smoothed_v) > 0.03f) {
                float adj = 0.05f;
                if(weight_voltage > adj) weight_voltage = adj;
                weight_coulomb = 1.0f - weight_voltage;
            }

            float blended = (weight_coulomb * coulomb_soc) + (weight_voltage * v_soc);

            if(blended < 0.0f) blended = 0.0f;
            if(blended > 100.0f) blended = 100.0f;

            float max_delta_per_sec = 2.0f; 
            if(in_transient) max_delta_per_sec = 0.5f;
            float dt_sec = (float)dt_ms / 1000.0f;
            if(dt_sec <= 0.0f) dt_sec = 1.0f;
            float max_delta = max_delta_per_sec * dt_sec;
            float delta = blended - curr_soc_percent;
            if(delta > max_delta) blended = curr_soc_percent + max_delta;
            else if(delta < -max_delta) blended = curr_soc_percent - max_delta;

            soc_percent = blended;

            if(i < -0.01f) { 
                if(soc_percent > curr_soc_percent) {
                    soc_percent = curr_soc_percent; 
                }
            } else if(i > 0.01f) { 
                if(soc_percent < curr_soc_percent) {
                    soc_percent = curr_soc_percent; 
                }
            }

            const float ALPHA = 0.25f; 
            curr_soc_percent = (ALPHA * soc_percent) + ((1.0f - ALPHA) * curr_soc_percent);
            
            // Minimal Periodic Log for Battery/Discharge
            static uint32_t last_log_dis = 0;
            if (now - last_log_dis > 3000) {
                FURI_LOG_I(TAG, "BAT V:%.2f I:%.2f j:%.2f SOC:%d", (double)v, (double)i, (double)v_jump, (int)curr_soc_percent);
                last_log_dis = now;
            }

            return (uint8_t)(curr_soc_percent + 0.5f);
        }
    }
#endif

    uint8_t pct = 90;
    FuriHalAdcHandle* handle = furi_hal_adc_acquire();
    if(!handle) return pct;
    furi_hal_adc_configure(handle);
    uint16_t raw_vbat = furi_hal_adc_read(handle, FuriHalAdcChannelVBAT);
    uint16_t raw_vref = furi_hal_adc_read(handle, FuriHalAdcChannelVREFINT);
    float vref_mV = furi_hal_adc_convert_vref(handle, raw_vref);
    float adc_input_mV = ((float)raw_vbat) * vref_mV / 4095.0f;
    float vbat_mV = adc_input_mV * 3.0f;
    float vbat = vbat_mV / 1000.0f;
    furi_hal_adc_release(handle);
    if(vbat <= V_MIN) pct = 0;
    else if(vbat >= V_MAX) pct = 100;
    else {
        float t = (vbat - V_MIN) / (V_MAX - V_MIN);
        pct = (uint8_t)(t * 100.0f + 0.5f);
    }
    return pct;
}

uint8_t furi_hal_power_get_bat_health_pct(void) { return 100; }

bool furi_hal_power_is_charging(void) {
    #ifdef USE_INA219
    if(furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if(furi_hal_ina219_get_voltage_current(&v, &i)) {
            return (i > 0.0f);
        }
    }
    #endif
    return false;
}

bool furi_hal_power_is_charging_done(void) { return false; }

void furi_hal_power_shutdown(void) {
    while(1) {}
}

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
    if(furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if(furi_hal_ina219_get_voltage_current(&v, &i)) {
            float vbat = v - (i * R_INTERNAL);
            if(vbat < 0.0f) vbat = 0.0f;
            if(vbat > 4.2f) vbat = 4.2f;
            return vbat;
        }
    }
#endif
    FuriHalAdcHandle* handle = furi_hal_adc_acquire();
    if(!handle) return 3.7f;

    furi_hal_adc_configure(handle);
    uint16_t raw_vbat = furi_hal_adc_read(handle, FuriHalAdcChannelVBAT);
    uint16_t raw_vref = furi_hal_adc_read(handle, FuriHalAdcChannelVREFINT);
    float vref_mV = furi_hal_adc_convert_vref(handle, raw_vref);
    float adc_input_mV = ((float)raw_vbat) * vref_mV / 4095.0f;
    float vbat_mV = adc_input_mV * 3.0f;
    float vbat = vbat_mV / 1000.0f;
    furi_hal_adc_release(handle);

    if(vbat < 3.2f) vbat = 0.0f;
    if(vbat > 4.2f) vbat = 4.2f;

    return vbat;
}

float furi_hal_power_get_battery_current(FuriHalPowerIC ic) {
    (void)ic; 
#ifdef USE_INA219
    if(furi_hal_ina219_is_ready()) {
        float v = 0.0f, i = 0.0f;
        if(furi_hal_ina219_get_voltage_current(&v, &i)) return i;
    }
#endif
    const uint32_t SAMPLE_MS = 250;
    float v1 = furi_hal_power_get_battery_voltage(ic);
    furi_delay_ms(SAMPLE_MS);
    float v2 = furi_hal_power_get_battery_voltage(ic);

    float dv = v2 - v1;
    float dt = SAMPLE_MS / 1000.0f; 
    if(dt <= 0.0f) return 0.0f;

    const float V_MIN = 3.00f;
    const float V_MAX = 4.20f;
    float capacity_mAh = (float)furi_hal_power_get_battery_full_capacity();
    float capacity_Ah = capacity_mAh / 1000.0f;

    float Ceq = (capacity_Ah * 3600.0f) / (V_MAX - V_MIN);
    float i_ma = (Ceq * (dv / dt)) * 1000.0f;
    
    if(i_ma > 5000.0f) i_ma = 5000.0f;
    if(i_ma < -5000.0f) i_ma = -5000.0f;
    UNUSED(i_ma);
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