#include "esp_log.h"
#include "OutputController.hpp"
#include "FactoryRunner.hpp"
#include "FactoryApi.hpp"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include <cstring>

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    wifi_config_t wifi_config = {};
    snprintf((char*)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "LumiGrid-Factory-%02X%02X%02X", mac[3], mac[4], mac[5]);
    wifi_config.ap.ssid_len = strlen((char*)wifi_config.ap.ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.password[0] = 0; // Open network
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("lumigrid-factory", "WiFi SoftAP started. SSID:%s", wifi_config.ap.ssid);
}

extern "C" void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- GPIO INPUT TEST ---
    ESP_LOGI("gpio-test", "Configuring GPIO 21 (SDA) and 22 (SCL) as inputs...");
    gpio_reset_pin(GPIO_NUM_21);
    gpio_reset_pin(GPIO_NUM_22);
    gpio_set_direction(GPIO_NUM_21, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_22, GPIO_MODE_INPUT);
    for (int i = 0; i < 15; i++) {
        ESP_LOGW("gpio-test", "GPIO 21 (SDA) = %d, GPIO 22 (SCL) = %d", 
                 gpio_get_level(GPIO_NUM_21), gpio_get_level(GPIO_NUM_22));
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    static lg::OutputController output;
    static lg::FactoryRunner factory(output);
    static lg::FactoryApi api(factory);

    ESP_ERROR_CHECK(output.init());

    // Initial proof that OE, PCA init, and ALED reset state work.
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_ERROR_CHECK(output.blackout());

    wifi_init_softap();
    factory.start();
    api.start();

    ESP_LOGI(
        "lumigrid-factory",
        "Factory test active: 8 ALED lanes + 8 PWM channels + REST API"
    );
}
