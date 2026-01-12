#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "capport.h"

void wifi_start_ap_fallback(void);

static const char *TAG = "WIFI";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5

typedef struct {
    char ssid[33];
    char pass[65];
} wifi_creds_t;

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static wifi_creds_t s_creds = {0};
static bool s_netif_ready = false;
static bool s_ap_created  = false;

static void ensure_netif_once(void) {
    if (!s_netif_ready) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_netif_ready = true;
    }
}

static bool ap_netif_exists(void) {
    return esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") != NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to AP (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to AP");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_load_creds_from_nvs(wifi_creds_t* creds) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    size_t ssid_len = sizeof(creds->ssid);
    size_t pass_len = sizeof(creds->pass);
    
    err = nvs_get_str(nvs_handle, "ssid", creds->ssid, &ssid_len);
    if (err == ESP_OK) {
        nvs_get_str(nvs_handle, "pass", creds->pass, &pass_len);
    }
    
    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_save_creds_to_nvs(const wifi_creds_t* creds) {
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

esp_err_t wifi_start_sta(const wifi_creds_t* creds) {
    if (!creds || strlen(creds->ssid) == 0) {
        ESP_LOGE(TAG, "Invalid credentials");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_creds = *creds;
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_got_ip));
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, creds->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, creds->pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Wi-Fi STA started, connecting to SSID:%s", creds->ssid);
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to AP");
        wifi_start_ap_fallback();
        return ESP_FAIL;
    }
    
    return ESP_ERR_TIMEOUT;
}

void wifi_start_ap_fallback(void) {
    ensure_netif_once();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (!s_ap_created && !ap_netif_exists()) {
        (void)esp_netif_create_default_wifi_ap();
        s_ap_created = true;
    }

    // Get MAC address and format SSID as LumiGrid_Setup_XXXX
    uint8_t mac[6];
    char ssid_buf[32];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(ssid_buf, sizeof(ssid_buf), "LumiGrid_Setup_%02X%02X", mac[4], mac[5]);

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .password = "",
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
        }
    };
    strncpy((char*)ap_cfg.ap.ssid, ssid_buf, sizeof(ap_cfg.ap.ssid) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP started with SSID: %s", ssid_buf);
    
    // Start captive portal after AP is up
    esp_netif_ip_info_t ip_info;
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != NULL && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        capport_start(ip_info.ip.addr);
    }
}

void task_wifi_init(void) {
    wifi_creds_t creds = {0};
    
    if (wifi_load_creds_from_nvs(&creds) == ESP_OK && strlen(creds.ssid) > 0) {
        ESP_LOGI(TAG, "Found saved credentials, attempting STA mode");
        if (wifi_start_sta(&creds) != ESP_OK) {
            ESP_LOGW(TAG, "STA failed, falling back to AP mode");
            wifi_start_ap_fallback();
        }
    } else {
        ESP_LOGI(TAG, "No saved credentials, starting AP mode");
        wifi_start_ap_fallback();
    }
}

void wifi_start_setup_wizard(void) {
    ESP_LOGI(TAG, "Starting Setup Wizard mode");
    wifi_start_ap_fallback();
}
