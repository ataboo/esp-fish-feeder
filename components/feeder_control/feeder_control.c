#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "feeder_control.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "time.h"
// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/queue.h"
#include "esp_timer.h"

#define STEP_COUNT 4
#define STEP_DELAY_MS 5
#define BTN_COOLDOWN_US 250000

static uint64_t last_btn_1_down_us = 0;
static uint64_t last_btn_2_down_us = 0;

static QueueHandle_t button_queue;

static const char *TAG = "FEEDER_CONTROL";
static const bool STEPS[STEP_COUNT][4] = {
    {0, 0, 0, 1},
    // {1, 0, 0, 1},
    {0, 0, 1, 0},
    // {0, 0, 1, 1},
    {0, 1, 0, 0},
    // {0, 1, 1, 0},
    {1, 0, 0, 0},
    // {1, 1, 0, 0},
};

static int step_idx = 0;
static int position = 0;
static int target_pos = 0;
static bool callibrating = false;
static bool has_callibrated = false;

static void update_stepper_out()
{
    gpio_set_level(CONFIG_STEP1_GPIO, STEPS[step_idx][0]);
    gpio_set_level(CONFIG_STEP2_GPIO, STEPS[step_idx][1]);
    gpio_set_level(CONFIG_STEP3_GPIO, STEPS[step_idx][2]);
    gpio_set_level(CONFIG_STEP4_GPIO, STEPS[step_idx][3]);
}

static void step_forward()
{
    step_idx = (step_idx + 1) % STEP_COUNT;
    update_stepper_out();
    position++;
}

static void step_backwards()
{
    step_idx = (step_idx - 1 + STEP_COUNT) % STEP_COUNT;
    update_stepper_out();
    position--;
}

static void stop()
{
    gpio_set_level(CONFIG_STEP1_GPIO, 0);
    gpio_set_level(CONFIG_STEP2_GPIO, 0);
    gpio_set_level(CONFIG_STEP3_GPIO, 0);
    gpio_set_level(CONFIG_STEP4_GPIO, 0);
}

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(button_queue, &pinNumber, NULL);
}

void start_callibration()
{
    callibrating = true;
    ESP_LOGI(TAG, "Started callibration");
    has_callibrated = true;
}

static bool all_buckets_extended()
{
    return target_pos >= CONFIG_BUCKET_COUNT * CONFIG_STEPS_PER_BUCKET;
}

void extend_bucket()
{
    if (!all_buckets_extended() && target_pos <= position)
    {
        target_pos += (target_pos == 0 ? CONFIG_FIRST_BUCKET_STEPS : CONFIG_STEPS_PER_BUCKET);
        ESP_LOGI(TAG, "Next bucket: %d", target_pos);
    }
}

void eject_buckets()
{
    has_callibrated = false;
    if (target_pos <= position)
    {
        target_pos += CONFIG_STEPS_PER_BUCKET * 3;
        ESP_LOGI(TAG, "Ejecting buckets: %d", target_pos);
    }
}

static void button_queue_task(void *params)
{
    int pinNumber;
    int level;
    while (true)
    {
        if (xQueueReceive(button_queue, &pinNumber, portMAX_DELAY))
        {
            if (pinNumber == CONFIG_LIMIT_GPIO)
            {
                level = gpio_get_level(pinNumber);
                if (callibrating && level == 0)
                {
                    ESP_LOGI(TAG, "Callibration end");
                    position = 0;
                    target_pos = 0;
                    callibrating = false;
                }
            }
            else if ( pinNumber == CONFIG_EXTEND_BTN_GPIO)
            {
                uint64_t now_us = esp_timer_get_time();
                bool on_cooldown = now_us - last_btn_1_down_us < BTN_COOLDOWN_US;
                last_btn_1_down_us = now_us;

                if(!on_cooldown) {
                    ESP_LOGI(TAG, "BTN 1");
                    if (all_buckets_extended())
                    {
                        eject_buckets();
                    }
                    else if (CONFIG_EXTEND_BUTTON_ACTIVE)
                    {
                        extend_bucket();
                    }
                }
            }
            else if (!has_callibrated && pinNumber == CONFIG_RETRACT_BTN_GPIO)
            {
                uint64_t now_us = esp_timer_get_time();
                bool on_cooldown = now_us - last_btn_2_down_us < BTN_COOLDOWN_US;
                last_btn_2_down_us = now_us;
                if(!on_cooldown) {
                    ESP_LOGI(TAG, "BTN 2");
                    level = gpio_get_level(CONFIG_LIMIT_GPIO);
                    if (level == 1)
                    {
                        start_callibration();
                    }
                }
            }
        }
    }

    vTaskDelete(NULL);
}

static void target_step_task(void *params)
{
    while (true)
    {
        if (callibrating || target_pos < position)
        {
            step_backwards();
        }
        else if (target_pos > position)
        {
            step_forward();
        }
        else
        {
            stop();
        }

        vTaskDelay(STEP_DELAY_MS / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static esp_err_t init_button_control()
{
    gpio_config_t in_conf = {};
    in_conf.intr_type = GPIO_INTR_POSEDGE;
    in_conf.mode = GPIO_MODE_INPUT;
    in_conf.pin_bit_mask = (1ULL << CONFIG_EXTEND_BTN_GPIO | 1ULL << CONFIG_RETRACT_BTN_GPIO | 1ULL << CONFIG_LIMIT_GPIO);
    in_conf.pull_down_en = 0;
    in_conf.pull_up_en = 1;
    esp_err_t config_err = gpio_config(&in_conf);
    if (config_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to config gpio input: %d", config_err);
        return config_err;
    }

    config_err |= gpio_set_intr_type(CONFIG_LIMIT_GPIO, GPIO_INTR_NEGEDGE);

    button_queue = xQueueCreate(10, sizeof(int));
    xTaskCreate(button_queue_task, "Button queue task", 2048, NULL, 5, NULL);
    xTaskCreate(target_step_task, "Target step task", 2048, NULL, 5, NULL);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_EXTEND_BTN_GPIO, gpio_interrupt_handler, (void *)CONFIG_EXTEND_BTN_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_RETRACT_BTN_GPIO, gpio_interrupt_handler, (void *)CONFIG_RETRACT_BTN_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_LIMIT_GPIO, gpio_interrupt_handler, (void *)CONFIG_LIMIT_GPIO));

    return config_err;
}

static esp_err_t init_motor_control()
{
    gpio_config_t out_conf = {};
    out_conf.intr_type = GPIO_INTR_DISABLE;
    out_conf.mode = GPIO_MODE_OUTPUT;
    out_conf.pin_bit_mask = (1LL << CONFIG_STEP1_GPIO | 1LL << CONFIG_STEP2_GPIO | 1LL << CONFIG_STEP3_GPIO | 1LL << CONFIG_STEP4_GPIO);
    out_conf.pull_down_en = 0;
    out_conf.pull_up_en = 0;
    esp_err_t config_err = gpio_config(&out_conf);

    return config_err;
}

void feeder_control_init()
{
    ESP_ERROR_CHECK(init_motor_control());
    ESP_ERROR_CHECK(init_button_control());
}
