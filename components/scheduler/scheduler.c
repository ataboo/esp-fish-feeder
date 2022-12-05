// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "scheduler.h"
#include "wifi_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "time.h"
#include "esp_sleep.h"

#define CLOCK_UPDATE_COOLDOWN_MINS (60)

static const char *TAG = "FISH_FEED_SCHEDULER";

static int64_t clock_update_cooldown_micros;
static int64_t last_update_micros;
static time_t now = 0;
static uint16_t sleep_time_mins;
static struct tm timeinfo;

static int hm_to_mins(int hour_mins)
{
    return (hour_mins / 100 * 60) + (hour_mins % 100);
}

// static float level_for_time(int mins)
// {
//     if (mins > sunrise_start_mins && mins < sunset_end_mins)
//     {
//         if (mins < sunrise_end_mins)
//         {
//             return (float)(mins - sunrise_start_mins) / (sunrise_end_mins - sunrise_start_mins);
//         }

//         if (mins > sunset_start_mins)
//         {
//             return 1 - (float)(mins - sunset_start_mins) / (sunset_end_mins - sunset_start_mins);
//         }

//         return 1;
//     }

//     return 0;
// }

static uint16_t mins_until_next_wakeup()
{

    return 1;

    // If the data line gets less noise, will use something like this again.
    //  int mins = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    // if(mins < sunrise_start_mins) {
    //     return (sunrise_start_mins - mins);
    // }

    // if (mins < sunrise_end_mins) {
    //     return 1;
    // }

    // if (mins < sunset_start_mins) {
    //     return sunset_start_mins - mins;
    // }

    // if (mins < sunset_end_mins) {
    //     return 1;
    // }

    // return 24 * 60 - mins + sunrise_start_mins;
}

static void update_internal_clock()
{
    esp_err_t ret = blocking_update_time();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to update time");
    }
    else
    {
        ESP_LOGD(TAG, "successfully updated clock");
        last_update_micros = esp_timer_get_time();
    }
}

static void scheduler_loop_task(void *arg)
{
    ESP_LOGD(TAG, "Started update task");
    while (true)
    {
        if (esp_timer_get_time() - last_update_micros > clock_update_cooldown_micros)
        {
            update_internal_clock();
        }

        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "Updating for time: %.2d:%.2d", timeinfo.tm_hour, timeinfo.tm_min);

#ifdef CONFIG_SLEEP_ACTIVE
        sleep_time_mins = mins_until_next_wakeup();
        ESP_LOGI(TAG, "Sleeping for %d seconds", sleep_time_mins * 60);
        esp_sleep_enable_timer_wakeup((uint64_t)sleep_time_mins * 60e6);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        esp_light_sleep_start();
#else
        vTaskDelay(60000 / portTICK_PERIOD_MS);
#endif
    }

    vTaskDelete(NULL);
}

static void scheduler_init()
{
    clock_update_cooldown_micros = CLOCK_UPDATE_COOLDOWN_MINS * 60 * 1e6;
    update_internal_clock();
}

esp_err_t scheduler_start()
{
    scheduler_init();

    xTaskCreate(scheduler_loop_task, "scheduler loop", 4096, NULL, 5, NULL);

    return ESP_OK;
}
