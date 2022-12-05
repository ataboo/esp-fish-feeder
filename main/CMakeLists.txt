idf_component_register(SRCS "esp_fish_feeder.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_time scheduler feeder_control)