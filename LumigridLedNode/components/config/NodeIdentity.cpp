#include "NodeIdentity.hpp"
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM)
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#endif

namespace lg {

NodeIdentity get_node_identity(const char* device_name) {
    NodeIdentity identity{};
    std::strncpy(identity.family, "led-node", sizeof(identity.family) - 1);
    std::strncpy(identity.model, "lg-led-v1", sizeof(identity.model) - 1);
    std::strncpy(identity.hw_revision, "A", sizeof(identity.hw_revision) - 1);

    const char* fw = "1.0.0";
#if defined(ESP_PLATFORM)
    const esp_app_desc_t* app = esp_app_get_description();
    if (app && app->version[0] != '\0') {
        fw = app->version;
    }
#endif
    std::strncpy(identity.fw_version, fw, sizeof(identity.fw_version) - 1);

    if (device_name && device_name[0] != '\0') {
        std::strncpy(identity.name, device_name, sizeof(identity.name) - 1);
    } else {
        std::strncpy(identity.name, "Lumigrid LED Node", sizeof(identity.name) - 1);
    }

    uint8_t mac[6] = {0x84, 0xf7, 0x03, 0xc2, 0x91, 0x10};
#if defined(ESP_PLATFORM)
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif

    std::snprintf(
        identity.id,
        sizeof(identity.id),
        "lg-%02x%02x%02x%02x%02x%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );

    return identity;
}

} // namespace lg
