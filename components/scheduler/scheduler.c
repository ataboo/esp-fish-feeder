// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "scheduler.h"
#include "wifi_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "time.h"
#include "esp_sleep.h"
#include "feeder_control.h"
#include "sdkconfig.h"
#include "buzzer_music.h"
#include "buzzer_control.h"

#define CLOCK_UPDATE_COOLDOWN_MINS (60)

static const char *TAG = "FISH_FEED_SCHEDULER";

static int64_t clock_update_cooldown_micros;
static int64_t last_clock_update_micros;
static time_t now = 0;
static uint16_t sleep_time_mins;
static struct tm timeinfo;

static int feeding_time_mins;
static int minute_of_day;
static int last_tick_minutes;

static buzzer_pattern_t* boot_music;
static buzzer_pattern_t* ready_music;
static buzzer_pattern_t* feed_music;

static int hm_to_mins(int hour_mins)
{
    return (hour_mins / 100 * 60) + (hour_mins % 100);
}

static uint16_t mins_until_next_wakeup()
{
    return 1;
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
        last_clock_update_micros = esp_timer_get_time();
    }
}

static void scheduler_loop_task(void *arg)
{
    ESP_LOGD(TAG, "Started update task");
    while (true)
    {
        if (esp_timer_get_time() - last_clock_update_micros > clock_update_cooldown_micros)
        {
            update_internal_clock();
        }

        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "Updating for time: %.2d:%.2d", timeinfo.tm_hour, timeinfo.tm_min);

        minute_of_day = timeinfo.tm_hour * 60 + timeinfo.tm_min;

        ESP_LOGW(TAG, "minute: %d, feeding: %d, last min: %d", minute_of_day, feeding_time_mins, last_tick_minutes);
        if (last_tick_minutes > 0 && minute_of_day >= feeding_time_mins && last_tick_minutes < feeding_time_mins)
        {
            ESP_LOGI(TAG, "Feeding time!");
            extend_bucket();
            buzzer_control_play_pattern(boot_music);
        }

        last_tick_minutes = minute_of_day;
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

static void init_music() {
    parse_music_str("o3l2co4c", &boot_music);
    boot_music->waveform = BUZZER_WAV_SQUARE;
    boot_music->loop = false;

    parse_music_str("o3l1cr1fr1ar1o4cr1cccr1o3ar1aaar1fr1ar1fr1l2c", &ready_music);
    ready_music->waveform = BUZZER_WAV_SQUARE;
    ready_music->loop = false;

    parse_music_str("o3l2cgo4er1o3cgo4er1", &feed_music);
    feed_music->waveform = BUZZER_WAV_SQUARE;
    feed_music->loop = false;
}

static void scheduler_init()
{
    feeding_time_mins = hm_to_mins(CONFIG_FEEDING_TIME);
    clock_update_cooldown_micros = CLOCK_UPDATE_COOLDOWN_MINS * 60 * 1e6;
    update_internal_clock();
}

esp_err_t scheduler_start()
{
    init_music();
    ESP_ERROR_CHECK_WITHOUT_ABORT(buzzer_control_init());
    
    buzzer_control_play_pattern(boot_music);

    scheduler_init();

    buzzer_control_play_pattern(ready_music);

    xTaskCreate(scheduler_loop_task, "scheduler loop", 4096, NULL, 5, NULL);

    return ESP_OK;
}
