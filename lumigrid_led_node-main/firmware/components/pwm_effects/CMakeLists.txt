idf_component_register(
    SRCS "pwm_effects.c"
    INCLUDE_DIRS "include"
    REQUIRES pca9685_driver esp_timer
)
