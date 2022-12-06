#pragma once

#include "esp_err.h"
#include <stdbool.h>


typedef enum {
    BUZZER_WAV_SQUARE = 0,
    BUZZER_WAV_SIN,
    BUZZER_WAV_SAW,
} buzzer_waveform_t;

typedef struct {
    uint16_t frequency;
    uint16_t duration;
} buzzer_keyframe_t;

typedef struct {
    bool loop;
    int frame_count;
    buzzer_waveform_t waveform;
    buzzer_keyframe_t* key_frames;
} buzzer_pattern_t;

esp_err_t buzzer_control_init();

esp_err_t buzzer_control_play_pattern(buzzer_pattern_t* pattern);