#include "rest_api.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "json_config.h"
#include <string.h>

static const char* TAG = "REST_SETUP";

// Local WiFi credentials structure (same as in task_wifi.c)
typedef struct {
    char ssid[33];
    char pass[65];
} wifi_creds_t;

// Save WiFi credentials to NVS (duplicated from task_wifi.c to avoid dependency)
static esp_err_t save_wifi_creds_to_nvs(const wifi_creds_t* creds) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_set_str(nvs_handle, "ssid", creds->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, "pass", creds->pass);
    }
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return err;
}

// WiFi scan endpoint - GET /api/wifi/scan
static esp_err_t http_wifi_scan(httpd_req_t *req) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"WiFi scan failed\"}");
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_OK;
    }

    // Limit to reasonable number
    if (ap_count > 20) {
        ap_count = 20;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"Out of memory\"}");
        return ESP_OK;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    
    for (int i = 0; i < ap_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "channel", ap_records[i].primary);
        
        const char *auth_mode = "OPEN";
        if (ap_records[i].authmode == WIFI_AUTH_WEP) auth_mode = "WEP";
        else if (ap_records[i].authmode == WIFI_AUTH_WPA_PSK) auth_mode = "WPA";
        else if (ap_records[i].authmode == WIFI_AUTH_WPA2_PSK) auth_mode = "WPA2";
        else if (ap_records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK) auth_mode = "WPA/WPA2";
        else if (ap_records[i].authmode == WIFI_AUTH_WPA3_PSK) auth_mode = "WPA3";
        
        cJSON_AddStringToObject(network, "auth", auth_mode);
        cJSON_AddItemToArray(networks, network);
    }
    
    cJSON_AddItemToObject(root, "networks", networks);
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    free(ap_records);
    
    return ESP_OK;
}

// Setup complete endpoint - POST /api/setup/complete
static esp_err_t http_setup_complete(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Invalid request\"}");
        return ESP_OK;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    // Extract configuration
    cJSON *wifi_ssid = cJSON_GetObjectItem(json, "wifi_ssid");
    cJSON *wifi_password = cJSON_GetObjectItem(json, "wifi_password");
    cJSON *node_id = cJSON_GetObjectItem(json, "node_id");
    cJSON *control_mode = cJSON_GetObjectItem(json, "control_mode");
    cJSON *master_ip = cJSON_GetObjectItem(json, "master_ip");

    bool success = true;
    const char *error_msg = NULL;

    // Validate required fields
    if (!cJSON_IsString(wifi_ssid) || !cJSON_IsString(wifi_password) || 
        !cJSON_IsString(node_id) || !cJSON_IsString(control_mode)) {
        success = false;
        error_msg = "Missing required fields";
    } else {
        // Save WiFi credentials to NVS
        wifi_creds_t creds = {0};
        strncpy(creds.ssid, wifi_ssid->valuestring, sizeof(creds.ssid) - 1);
        strncpy(creds.pass, wifi_password->valuestring, sizeof(creds.pass) - 1);
        
        esp_err_t err = save_wifi_creds_to_nvs(&creds);
        if (err != ESP_OK) {
            success = false;
            error_msg = "Failed to save WiFi credentials";
            ESP_LOGE(TAG, "Failed to save WiFi creds: %s", esp_err_to_name(err));
        }

        // Save node configuration
        if (success && config_lock()) {
            cJSON *root = config_mutable_root();
            if (root) {
                // Update node ID
                cJSON_DeleteItemFromObject(root, "node_id");
                cJSON_AddStringToObject(root, "node_id", node_id->valuestring);
                
                // Update control mode
                cJSON_DeleteItemFromObject(root, "control_mode");
                cJSON_AddStringToObject(root, "control_mode", control_mode->valuestring);
                
                // Update master IP if slave mode
                if (cJSON_IsString(master_ip) && strlen(master_ip->valuestring) > 0) {
                    cJSON_DeleteItemFromObject(root, "master_ip");
                    cJSON_AddStringToObject(root, "master_ip", master_ip->valuestring);
                }
            }
            config_unlock();
            
            // Mark setup as complete
            err = config_mark_setup_complete();
            if (err != ESP_OK) {
                success = false;
                error_msg = "Failed to save configuration";
                ESP_LOGE(TAG, "Failed to mark setup complete: %s", esp_err_to_name(err));
            }
        } else {
            success = false;
            error_msg = "Failed to acquire config lock";
        }
    }

    cJSON_Delete(json);

    // Send response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", success);
    if (error_msg) {
        cJSON_AddStringToObject(response, "error", error_msg);
    }
    
    char *resp_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    
    free(resp_str);
    cJSON_Delete(response);

    // Schedule reboot if successful
    if (success) {
        ESP_LOGI(TAG, "Setup complete, rebooting in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    return ESP_OK;
}

void rest_api_setup_register(httpd_handle_t server) {
    if (!server) {
        ESP_LOGE(TAG, "Cannot register setup endpoints: server is NULL");
        return;
    }

    const httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = http_wifi_scan,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_scan_uri);

    const httpd_uri_t setup_complete_uri = {
        .uri = "/api/setup/complete",
        .method = HTTP_POST,
        .handler = http_setup_complete,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &setup_complete_uri);

    ESP_LOGI(TAG, "Setup wizard API endpoints registered");
}
