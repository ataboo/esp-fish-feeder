#include "buzzer_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/dac_cosine.h"
#include "driver/dac_continuous.h"
#include "esp_check.h"
#include <math.h>
#include "esp_timer.h"

#define CONST_PERIOD_2_PI           6.2832

#define EXAMPLE_ARRAY_LEN       400
#define EXAMPLE_DAC_AMPLITUDE   255
#define EXAMPLE_WAVE_FREQ_HZ    2000                      // Default wave frequency 2000 Hz, it can't be too low
#define EXAMPLE_CONVERT_FREQ_HZ (EXAMPLE_ARRAY_LEN * EXAMPLE_WAVE_FREQ_HZ) // The frequency that DAC convert every data in the wave array

#define TASK_N_QUIT (1ULL << 1)
#define TASK_N_RESET (1ULL << 2)

uint8_t sin_wav[EXAMPLE_ARRAY_LEN];

dac_continuous_handle_t cont_handle;

static const char *TAG = "BUZZER_CONTROL";
static buzzer_pattern_t *current_pattern = NULL;
static buzzer_keyframe_t *current_keyframe = NULL;
static int current_keyframe_idx = 0;
static int64_t next_frame_time_us;
static TaskHandle_t buzzer_task_handle;


static void gen_approx_wavs()
{
    for (int i = 0; i < EXAMPLE_ARRAY_LEN; i++)
    {
        sin_wav[i] = (uint8_t)((sin(i * CONST_PERIOD_2_PI / EXAMPLE_ARRAY_LEN) + 1) * (double)(EXAMPLE_DAC_AMPLITUDE) / 2 + 0.5);
    }
}

static void buzzer_stop_play() {
    if(cont_handle == NULL) {
        return;
    }

    dac_continuous_disable(cont_handle);
    dac_continuous_del_channels(cont_handle);
    cont_handle = NULL;
}

static void buzzer_start_play(uint16_t freq) {
    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH1,
        .desc_num = 8,
        .buf_size = 2048,
        .freq_hz = EXAMPLE_ARRAY_LEN * freq,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,     // If the frequency is out of range, try 'DAC_DIGI_CLK_SRC_APLL'
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    /* Allocate continuous channel */
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &cont_handle));
    /* Enable the channels in the group */
    ESP_ERROR_CHECK(dac_continuous_enable(cont_handle));
    
    ESP_ERROR_CHECK(dac_continuous_write_cyclically(cont_handle, (uint8_t*) sin_wav, EXAMPLE_ARRAY_LEN, NULL));
}

static bool increment_pattern_frame()
{
    if (current_keyframe == NULL)
    {
        return false;
    }

    if (esp_timer_get_time() >= next_frame_time_us)
    {
        current_keyframe_idx++;
        if (current_keyframe_idx == current_pattern->frame_count)
        {
            current_keyframe_idx = 0;

            if (!current_pattern->loop)
            {
                current_keyframe = NULL;
                return true;
            }
        }

        current_keyframe = &current_pattern->key_frames[current_keyframe_idx];

        return true;
    }

    return false;
}

static void buzzer_play_task(void *args)
{
    uint32_t notification = 0;
    bool changed_keyframe = false;
    int64_t current_time_us;

    while (true)
    {
        if (xTaskNotifyWait(1, 1, &notification, 10 / portTICK_PERIOD_MS))
        {
            if (notification & TASK_N_QUIT)
            {
                ESP_LOGI(TAG, "Quitting buzzer loop.");
                break;
            }

            if (notification & TASK_N_RESET)
            {
                ESP_LOGI(TAG, "Resetting");
                current_keyframe_idx = 0;
                if (current_pattern != NULL)
                {
                    current_keyframe = &current_pattern->key_frames[0];
                }
                else
                {
                    current_keyframe = NULL;
                }

                next_frame_time_us = 0;
                changed_keyframe = true;
            }
        }
        else
        {
            if (current_pattern != NULL && increment_pattern_frame())
            {
                changed_keyframe = true;
            }
        }

        if (changed_keyframe)
        {
            buzzer_stop_play();
            vTaskDelay(10 / portTICK_PERIOD_MS);

            current_time_us = esp_timer_get_time();
            if (current_keyframe != NULL)
            {
                next_frame_time_us = current_time_us + current_keyframe->duration * 1000;

                if (current_keyframe->frequency > 0)
                {
                    buzzer_start_play(current_keyframe->frequency);
                }
            }
            changed_keyframe = false;
        }
    }

    vTaskDelete(NULL);
}

esp_err_t buzzer_control_init() {
    gen_approx_wavs();

    xTaskCreate(buzzer_play_task, "Buzzer Task", 2048, NULL, 5, &buzzer_task_handle);

    return ESP_OK;
}

esp_err_t buzzer_control_play_pattern(buzzer_pattern_t* pattern) {
    current_pattern = pattern;
    xTaskNotify(buzzer_task_handle, TASK_N_RESET, eSetBits);

    return ESP_OK;
}

void buzzer_control_deinit()
{
    xTaskNotify(buzzer_task_handle, 1, eSetBits);

    current_pattern = NULL;
}

// void start_dac_cos()
// {
//     dac_cosine_handle_t chan0_handle;
//     dac_cosine_config_t cos0_cfg = {
//         .chan_id = DAC_CHAN_1,
//         .freq_hz = 1000,
//         .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
//         .offset = 0,
//         .phase = DAC_COSINE_PHASE_0,
//         .atten = DAC_COSINE_ATTEN_DEFAULT,
//         .flags.force_set_freq = false,
//     };


//     ESP_ERROR_CHECK(dac_cosine_new_channel(&cos0_cfg, &chan0_handle));
//     ESP_ERROR_CHECK(dac_cosine_start(chan0_handle));

//     vTaskDelay(10000/portTICK_PERIOD_MS);
// }
