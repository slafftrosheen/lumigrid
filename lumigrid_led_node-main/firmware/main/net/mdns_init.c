#include "mdns.h"
#include "esp_log.h"
#include "mdns_init.h"

static const char* TAG = "mDNS";

void mdns_start(const char* hostname, const char* instance, uint16_t port){
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init: %s", esp_err_to_name(err));
        return;
    }
    if (hostname && *hostname) mdns_hostname_set(hostname);
    if (instance && *instance) mdns_instance_name_set(instance);

    // _lumigrid._tcp for easy discovery
    mdns_service_add(NULL, "_lumigrid", "_tcp", port, NULL, 0);
    mdns_service_txt_item_set("_lumigrid", "_tcp", "ver", "1.0.0");
    mdns_service_txt_item_set("_lumigrid", "_tcp", "hw",  "esp32");
    ESP_LOGI(TAG, "mDNS up: %s (%s) on port %u", hostname, instance, (unsigned)port);
}
