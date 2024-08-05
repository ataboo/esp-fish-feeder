idf_component_register(SRCS "buzzer_dac.c" "buzzer_music.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_dac driver esp_timer)