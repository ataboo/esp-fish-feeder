idf_component_register(SRCS "scheduler.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES wifi_time feeder_control buzzer_control
                       REQUIRES )