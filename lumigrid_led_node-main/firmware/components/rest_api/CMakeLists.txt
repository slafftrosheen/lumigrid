idf_component_register(
    SRCS "rest_api.c" "rest_api_sequencer.c" "rest_api_pwm.c" "rest_api_setup.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "ui/index.html"
    REQUIRES esp_http_server json storage_fs led_effects esp_timer app_update effect_control palettes ota mqttc nodesync graph webdav metrics sequencer pwm_effects pca9685_driver nvs_flash esp_wifi esp_system
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
