#include "buzzer_music.h"
#include "buzzer_control.h"
#include <string.h>
#include "esp_log.h"

#define MAX_NOTE_COUNT 512

typedef enum {
    NOTE_MOD_NONE = 0,
    NOTE_MOD_SHARP = 1,
    NOTE_MOD_FLAT = -1
} note_mod_t;

static const char* TAG = "BUZZER_MUSIC";

static float note_frequencies[ ] = {
    16,    17,   18,   19,   20,  21.8,   23, 24.5, 25.9, 27.5, 29.1, 30.8,
    33,    34,   36,   38,   41,  43.6,   46, 48.9, 51.9,   55, 58.2, 61.7,
    65,    69,   73,   77,   82,  87.3,   92, 97.9,  103,  110,  116,  123,
    131,  138,  146,  155,  164,  174,   184,  195,  207,  220,  233,  246,
    261,  277,  293,  311,  329,  349,   369,  391,  415,  440,  466,  493,
    523,  554,  587,  622,  659,  698,   739,  783,  830,  880,  932,  987,
    1046, 1108, 1174, 1244, 1318, 1396, 1479, 1567, 1161, 1760, 1864, 1975,
    2093, 2217, 2349, 2489, 2637, 2793, 2959, 3135, 3324, 3520, 3729, 3951,
    4186, 4434, 4698, 4978, 5374, 5587, 5919, 6271, 6644, 7040, 7458, 7902,
};

static int whole_note_offsets[ ] = {
    9,  //a
    11, //b
    0,  //c
    2,  //d
    4,  //e
    5,  //f
    7   //g
};

static float frequency_for_note(char whole, note_mod_t modifier, uint8_t octive) {
    if (whole < 'a' || whole > 'g') {
        ESP_LOGE(TAG, "Whole note out of range.");
        return -1;
    }

    if ((whole == 'c' || whole == 'f') && modifier == NOTE_MOD_FLAT) {
        ESP_LOGE(TAG, "C and F flat are invalid.");
        return -1;
    }

    if ((whole == 'e' || whole == 'b') && modifier == NOTE_MOD_SHARP) {
        ESP_LOGE(TAG, "E and B sharp are invalid.");
        return -1;
    }

    int note_idx = whole_note_offsets[whole - 'a'] + modifier + octive * 12;

    return note_frequencies[note_idx];
}

static void buzzer_music_err_idx(const char* music_str, int err_idx) {
    ESP_LOGE(TAG, "Parse Error: %s\n%*s", music_str, 35 + err_idx, "^");
}

static uint8_t get_note_count(const char* music_str) {
    int note_count = 0;
    int len = strlen(music_str);
    for(int i=0; i<len; i++) {
        if((music_str[i] >= 'a' && music_str[i] <= 'g') || music_str[i] == 'r') {
            note_count++;
        }
    }

    return note_count;
}

static esp_err_t parse_notes(const char* music_str, buzzer_keyframe_t* frames, int note_count) {
    int len = strlen(music_str);
    int8_t octive = 3;
    int8_t note_len = 1;
    int idx = 0;
    uint16_t frame_count = 0;

    while(idx < len) {
        if (music_str[idx] >= 'a' && music_str[idx] <= 'g') {
            char whole = music_str[idx];
            note_mod_t modifier = NOTE_MOD_NONE;
            if(idx+1 < len) {
                if(music_str[idx+1] == '#') {
                    modifier = NOTE_MOD_SHARP;
                } else if(music_str[idx+1] == '$') {
                    modifier = NOTE_MOD_FLAT;
                }
            }

            if (frame_count == note_count) {
                ESP_LOGE(TAG, "Unnexpected note count when parsing.");
                buzzer_music_err_idx(music_str, idx);
                return ESP_FAIL;
            }

            float frequency = frequency_for_note(whole, modifier, octive);
            if (frequency < 0) {
                ESP_LOGE(TAG, "Failed to parse frequency.");
                buzzer_music_err_idx(music_str, idx);
            }

            frames[frame_count++] = (buzzer_keyframe_t){
                .duration = note_len * 1000 / 16,
                .frequency = (uint16_t)frequency,
            };

            idx++;
            if (modifier != NOTE_MOD_NONE) {
                idx++;
            }
        } else if (music_str[idx] == 'o') {
            octive = -1;
            char oct_char = music_str[idx+1];
            if (idx + 1 < len && oct_char > '0' && oct_char < '9') {
                octive = (int)oct_char - '0';
            }

            if (octive < 0) {
                ESP_LOGE(TAG, "Octive must have an integer following it.");
                buzzer_music_err_idx(music_str, idx);
                
                return ESP_FAIL;
            }

            idx += 2;
        } else if (music_str[idx] == 'l') {
            note_len = -1;
            char len_char = music_str[idx+1];
            if (idx + 1 < len && len_char > '0' && len_char < '9') {
                note_len = (int)len_char - '0';
            }

            if (note_len < 0) {
                ESP_LOGE(TAG, "Length must have an integer following it.");
                buzzer_music_err_idx(music_str, idx);
                
                return ESP_FAIL;
            }

            idx += 2;
        } else if (music_str[idx] == 'r') {
            uint8_t rest_len = -1;
            char len_char = music_str[idx+1];
            if (idx + 1 < len && len_char > '0' && len_char < '9') {
                rest_len = (int)len_char - '0';
            }

            if (rest_len < 0) {
                ESP_LOGE(TAG, "Rest must have an integer following it.");
                buzzer_music_err_idx(music_str, idx);
                
                return ESP_FAIL;
            }

            frames[frame_count++] = (buzzer_keyframe_t){
                .duration = rest_len * 1000 / 8,
                .frequency = 0,
            };

            idx += 2;
        } 
        
        else {
            ESP_LOGE(TAG, "Unnexpected character");
            buzzer_music_err_idx(music_str, idx);
            return ESP_FAIL;
        }
    }

    if (frame_count != note_count) {
        ESP_LOGE(TAG, "Unnexpected note count when parsing.");
        buzzer_music_err_idx(music_str, 0);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t buzzer_frequency_sweep(uint16_t start_freq, uint16_t end_freq, uint16_t step_count, uint16_t duration_ms, buzzer_pattern_t** pattern_out) {
    buzzer_keyframe_t* frames = malloc(sizeof(buzzer_keyframe_t) * step_count);
    
    for(int i=0; i<step_count; i++) {
        frames[i].duration = duration_ms / step_count,
        frames[i].frequency = ((end_freq - start_freq) * i / step_count) + start_freq;
    }

    buzzer_pattern_t* pattern = malloc(sizeof(buzzer_pattern_t));
    pattern->key_frames = frames;
    pattern->frame_count = step_count;

    *pattern_out = pattern;

    return ESP_OK;
}

esp_err_t parse_music_str(const char* music_str, buzzer_pattern_t** pattern_out) {
    int note_count = get_note_count(music_str);
    buzzer_keyframe_t* frames = malloc(sizeof(buzzer_keyframe_t) * note_count);

    if (parse_notes(music_str, frames, note_count) != ESP_OK) {
        return ESP_FAIL;
    }

    buzzer_pattern_t* pattern = malloc(sizeof(buzzer_pattern_t));
    pattern->key_frames = frames;
    pattern->frame_count = note_count;

    *pattern_out = pattern;

    return ESP_OK;
}