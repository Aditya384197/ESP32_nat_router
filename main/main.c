#include "router_core.h"
#include "esp_err.h"

void app_main(void)
{
    /*
     * No NVS initialization: Wi-Fi NVS storage is disabled in sdkconfig.
     * Credentials and router settings are compile-time constants.
     */
    ESP_ERROR_CHECK(router_core_init());

    /*
     * No application loop/task is required. Wi-Fi, lwIP and NAPT run in
     * their own system tasks. Returning from app_main releases its task.
     */
}
