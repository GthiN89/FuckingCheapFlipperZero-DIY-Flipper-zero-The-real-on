#include <furi.h>
#include <furi_hal.h>
#include <flipper.h>
#include <alt_boot.h>
#include <update_util/update_operation.h>
#include <furi_hal_mcp23017.h>

#define TAG "Main"

#define BUTTON_LEFT_BIT (1 << 4)  // 0x0010
#define BUTTON_UP_BIT   (1 << 7)  // 0x0080

uint16_t current_bits = 0x08FC;

int32_t init_task(void* context) {
    UNUSED(context);

    // Flipper FURI HAL
    furi_hal_init();

    // Init flipper
    flipper_init();

    furi_background();

    return 0;
}

int main(void) {
    // Initialize FURI layer
    furi_init();

    // Flipper critical FURI HAL
    furi_hal_init_early();

    furi_hal_set_is_normal_boot(false);
    FuriThread* main_thread = furi_thread_alloc_ex("InitSrv", 1024, init_task, NULL);
    furi_thread_set_priority(main_thread, FuriThreadPriorityInit);

#ifdef FURI_RAM_EXEC
    // Prevent entering sleep mode when executed from RAM
    furi_hal_power_insomnia_enter();
    furi_thread_start(main_thread);
#else
    furi_hal_light_sequence("RGB");

    // Delay is for button sampling
    furi_delay_ms(100);
    furi_hal_mcp23017_read_gpio(&current_bits);
      furi_delay_ms(100);
  //  bool left_pressed = !(current_bits & BUTTON_LEFT_BIT);
    bool up_pressed   = !(current_bits & BUTTON_UP_BIT);

    FuriHalRtcBootMode boot_mode = furi_hal_rtc_get_boot_mode();
    // if(boot_mode == FuriHalRtcBootModeDfu || left_pressed) {
    //     furi_hal_light_sequence("rgb WB");
    //     furi_hal_rtc_set_boot_mode(FuriHalRtcBootModeNormal);
    //     flipper_boot_dfu_exec();
    //     furi_hal_power_reset();
    // } 
    //else 
    if(boot_mode == FuriHalRtcBootModeUpdate) {
        furi_hal_light_sequence("rgb BR");
        // Do update
        flipper_boot_update_exec();
        // if things go nice, we shouldn't reach this point.
        // But if we do, abandon to avoid bootloops
        furi_hal_rtc_set_boot_mode(FuriHalRtcBootModeNormal);
        furi_hal_power_reset();
    } else if(up_pressed) {
        furi_hal_light_sequence("rgb WR");
        flipper_boot_recovery_exec();
        furi_hal_power_reset();
    } else {
        furi_hal_light_sequence("rgb G");
        if(boot_mode != FuriHalRtcBootModePostUpdate && boot_mode != FuriHalRtcBootModePreUpdate) {
            furi_hal_set_is_normal_boot(true);
        }
        furi_thread_start(main_thread);
    }
#endif

    // Run Kernel
    furi_run();

    furi_crash("Kernel is Dead");
}

void Error_Handler(void) {
    furi_crash("ErrorHandler");
}

void abort(void) {
    furi_crash("AbortHandler");
}