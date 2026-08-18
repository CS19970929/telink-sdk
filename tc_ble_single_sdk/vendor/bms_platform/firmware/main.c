#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"

#if (BLE_OTA_SERVER_ENABLE && BMS_OTA_LAYOUT_APPROVED)
#if ((BMS_OTA_FIRMWARE_SIZE_KB < 4) || (BMS_OTA_FIRMWARE_SIZE_KB > 252) || \
     ((BMS_OTA_FIRMWARE_SIZE_KB % 4) != 0) || \
     ((BMS_OTA_BOOT_ADDRESS != 0x20000u) && (BMS_OTA_BOOT_ADDRESS != 0x40000u)))
#error "Approved OTA requires reviewed BMS_OTA_FIRMWARE_SIZE_KB and BMS_OTA_BOOT_ADDRESS"
#endif
#endif

_attribute_ram_code_ void irq_handler(void)
{
    irq_blt_sdk_handler();
}

_attribute_ram_code_ int main(void)
{
    int deep_retention_wakeup;

    blc_pm_select_internal_32k_crystal();
#if (BLE_OTA_SERVER_ENABLE && BMS_OTA_LAYOUT_APPROVED)
    /* SDK requires this before cpu_wakeup_init(). */
    (void)blc_ota_setFirmwareSizeAndBootAddress(BMS_OTA_FIRMWARE_SIZE_KB,
                                                 (multi_boot_addr_e)BMS_OTA_BOOT_ADDRESS);
#endif
    cpu_wakeup_init();
    deep_retention_wakeup = pm_is_MCU_deepRetentionWakeup();
    rf_drv_ble_init();
    gpio_init(!deep_retention_wakeup);
    clock_init(SYS_CLK_TYPE);

    if (deep_retention_wakeup != 0) {
        user_init_deepRetn();
    } else {
        user_init_normal();
    }
    irq_enable();
    while (1) {
        main_loop();
    }
}
