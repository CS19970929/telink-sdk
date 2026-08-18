#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"

_attribute_ram_code_ void irq_handler(void)
{
    irq_blt_sdk_handler();
}

_attribute_ram_code_ int main(void)
{
    int deep_retention_wakeup;

    blc_pm_select_internal_32k_crystal();
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
