/********************************************************************************************************
 * @file    main.c
 * @brief   BLE SDK entry with HS-D011 BMS integration.
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "bms_project.h"

_attribute_ram_code_ void irq_handler(void)
{
    /* Keep BLE first; BMS IRQ processing only handles the UART DMA flags. */
    irq_blt_sdk_handler();
    bms_project_irq_handler();
}

_attribute_ram_code_ int main(void)
{
    int deepRetWakeUp;

    DBG_CHN0_LOW;
    blc_pm_select_internal_32k_crystal();

#if (MCU_CORE_TYPE == MCU_CORE_825x)
    cpu_wakeup_init();
#else
    cpu_wakeup_init(LDO_MODE, INTERNAL_CAP_XTAL24M);
#endif

    deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();
    rf_drv_ble_init();
    gpio_init(!deepRetWakeUp);
    clock_init(SYS_CLK_TYPE);

#if (MODULE_WATCHDOG_ENABLE)
    wd_set_interval_ms(WATCHDOG_INIT_TIMEOUT, CLOCK_SYS_CLOCK_1MS);
    wd_start();
#endif

    if (deepRetWakeUp) {
        user_init_deepRetn();
    } else {
        user_init_normal();
        /* BLE stack/GATT must exist before the compatibility layer updates ADV/scan data. */
        bms_project_init();
    }

    irq_enable();

    while (1) {
#if (MODULE_WATCHDOG_ENABLE)
#if (MCU_CORE_TYPE == MCU_CORE_TC321X)
        if (g_chip_version != CHIP_VERSION_A0)
#endif
        {
            wd_clear();
        }
#endif
        main_loop();
        bms_project_process();
    }
}
