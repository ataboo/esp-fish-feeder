// #define LOCAL_LOG_LEVEL ESP_LOG_DEBUG
#include <string.h>
#include <time.h>
#include "esp_system.h"
#include "esp_log.h"

#include "wifi_time.h"
#include "scheduler.h"
#include "feeder_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP_FISH_FEEDER";

static void indicate_failure()
{
    ESP_LOGE(TAG, "Fatal error on startup.  Resetting in 10 seconds.");

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    esp_restart();
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    feeder_control_init();
    scheduler_start();
}
