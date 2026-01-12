#include "lednode_init.h"
#include "board_pinmap.h"
#include "pca9685_driver.h"
#include "storage_fs.h"
#include "task_wifi.h"
#include "rest_api.h"
#include "ui_server.h"
#include "sync_protocol.h"
#include "mqtt_wrapper.h"
#include "task_effect_engine.h"
#include "task_pwm_driver.h"
#include "trigger_engine.h"
#include "self_test.h"
#include "net/mdns_init.h"
#include "json_config.h"
#include "aled_state.h"
#include "fs_utils.h"
#include "config_apply.h"
#include "scene_loader.h"
#include "cues.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "effects_registry.h"
#include "effects_extra.h"
#include "scheduler.h"
#include "selftest.h"
#include "layout.h"
#include "palettes.h"
#include "power.h"
#include "graph.h"
#include "sequencer.h"
#include "pwm_effects.h"
#include "mqttc.h"
#include "nodesync.h"
#include "cJSON.h"

#include <stddef.h>


static const char *TAG = "INIT";

void lednode_init(void) {
    ESP_LOGI(TAG, "=== LumiGrid LED Node Initialization ===");
    
    ESP_LOGI(TAG, "[1/8] Board GPIO setup");
    gpio_set_direction(PCA9685_OE, GPIO_MODE_OUTPUT);
    gpio_set_level(PCA9685_OE, 1);
    
    for (int i = 0; i < 4; i++) {
        gpio_set_direction(ALED_GPIO[i], GPIO_MODE_OUTPUT);
        gpio_set_level(ALED_GPIO[i], 0);
    }
    
    ESP_LOGI(TAG, "[2/8] Filesystem mount");
    esp_err_t fs_err = fsu_ensure_dirs();
    if (fs_err != ESP_OK) {
        ESP_LOGW(TAG, "Filesystem mount failed - continuing with in-memory defaults");
    }
    
    // Initialize config - will use in-memory defaults if filesystem unavailable
    esp_err_t cfg_err = config_init();
    if (cfg_err != ESP_OK) {
        ESP_LOGW(TAG, "Config init failed: %s - using minimal defaults", esp_err_to_name(cfg_err));
    }
    
    // Apply configuration to runtime
    esp_err_t apply_err = cfg_apply_all();
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "Config apply failed: %s - using runtime defaults", esp_err_to_name(apply_err));
    }
    layout_init();
    palettes_init();
    power_init();
    char scene_path[128];
    if (storage_fs_mount_path(scene_path, sizeof(scene_path), "scenes.json")) {
        (void)scene_apply_from_file(scene_path);
    }
    cues_init();
    aled_state_init_defaults(8, 120);
    
    ESP_LOGI(TAG, "[3/8] PCA9685 PWM driver");
    pca9685_config_t pca_cfg = {
        .i2c_addr = PCA9685_I2C_ADDR,
        .oe_pin = PCA9685_OE,
    };
    esp_err_t pca_err = pca9685_init(&pca_cfg);
    if (pca_err != ESP_OK) {
        ESP_LOGW(TAG, "PCA9685 not found at 0x%02X — continuing without PWM fixtures", PCA9685_I2C_ADDR);
    }
    
    ESP_LOGI(TAG, "[4/8] LED effects engine");
    fx_registry_init();
    fx_register_extra();
    graph_init();
    task_effect_engine_start();
    task_pwm_driver_start();
    selftest_init(0);  // GPIO 0 for button
    
    // Initialize sequencer and PWM effects
    sequencer_init();
    pwm_effects_init();
    
    for (int ch = 0; ch < 8; ch++) {
        pca9685_set_duty(ch, 0.0f);
    }
    
    ESP_LOGI(TAG, "[5/8] Wi-Fi connection");
    // Check if this is first boot and enter setup wizard if needed
    if (config_is_first_boot()) {
        ESP_LOGI(TAG, "First boot detected - entering Setup Wizard mode");
        wifi_start_setup_wizard();
    } else {
        task_wifi_init();
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "[6/8] HTTP server (REST + UI)");
    ESP_ERROR_CHECK(rest_api_start());
    
    // Register setup wizard UI if in first-boot mode
    if (config_is_first_boot()) {
        capport_register_endpoints(rest_api_get_handle());
    } else {
        ui_server_attach(rest_api_get_handle());
    }
    
    mdns_start("lumigrid-node", "LumiGrid LED Node", 80);
    
    ESP_LOGI(TAG, "[7/8] Communication protocols");
    sync_protocol_init();
    mqtt_wrapper_init();
    nodesync_init();
    mqttc_init();
    scheduler_init();
    
    ESP_LOGI(TAG, "[8/8] Self-test");
    pca9685_set_duty(0, 0.5f);
    vTaskDelay(pdMS_TO_TICKS(500));
    pca9685_set_duty(0, 0.0f);
    
    ESP_LOGI(TAG, "=== Initialization Complete ===");
    ESP_LOGI(TAG, "Access web UI: http://192.168.4.1 (AP mode)");
}


#include <string.h>
#include "effects.h"

// interpret strings and apply locally; persist if you want
void lednode_set_strip_type(int ch, const char *type, const char *order){
    if (ch < 0 || ch >= aled_count()) return;
    aled_channel_t *C = &aled_ch[ch];

    if (type){
        if (strcmp(type,"SK6812_RGBW")==0) C->type = LED_SK6812_RGBW;
        else                               C->type = LED_WS2812B;
    }
    if (order){
        if      (strcmp(order,"RGBW")==0)  C->order = ORDER_RGBW;
        else if (strcmp(order,"GRBW")==0)  C->order = ORDER_GRBW;
        else if (strcmp(order,"RGB")==0)   C->order = ORDER_RGB;
        else                               C->order = ORDER_GRB;
    }
    
    // Persist to config.json with mutex protection
    if (!config_lock()) {
        ESP_LOGW(TAG, "Failed to acquire config lock for strip type update");
        return;
    }
    
    cJSON* root = config_mutable_root();
    if (!root) {
        config_unlock();
        return;
    }
    
    // Get or create "strips" array
    cJSON* strips = cJSON_GetObjectItem(root, "strips");
    if (!strips) {
        strips = cJSON_AddArrayToObject(root, "strips");
        if (!strips) {
            config_unlock();
            return;
        }
    }
    
    // Find or create config for this channel
    cJSON* strip_cfg = NULL;
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, strips) {
        cJSON* ch_num = cJSON_GetObjectItem(item, "ch");
        if (cJSON_IsNumber(ch_num) && ch_num->valueint == ch) {
            strip_cfg = item;
            break;
        }
    }
    
    if (!strip_cfg) {
        strip_cfg = cJSON_CreateObject();
        if (!strip_cfg) {
            config_unlock();
            return;
        }
        cJSON_AddNumberToObject(strip_cfg, "ch", ch);
        cJSON_AddItemToArray(strips, strip_cfg);
    }
    
    // Update type and order in config
    if (type) {
        cJSON_DeleteItemFromObject(strip_cfg, "type");
        cJSON_AddStringToObject(strip_cfg, "type", type);
    }
    if (order) {
        cJSON_DeleteItemFromObject(strip_cfg, "order");
        cJSON_AddStringToObject(strip_cfg, "order", order);
    }
    
    config_unlock();
    
    // Save config to storage (config_save has its own locking)
    esp_err_t err = config_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save strip config: %s", esp_err_to_name(err));
    }
}
