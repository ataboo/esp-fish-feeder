#pragma once

#include "buzzer_control.h"

esp_err_t parse_music_str(const char* music_str, buzzer_pattern_t** pattern_out);

esp_err_t buzzer_frequency_sweep(uint16_t start_freq, uint16_t end_freq, uint16_t step_count, uint16_t duration_ms, buzzer_pattern_t** pattern_out);
