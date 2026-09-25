// components/network/MdnsService.cpp
#include "MdnsService.hpp"

#include "esp_check.h"
#include "mdns.h"

namespace lg {

esp_err_t MdnsService::start(const std::string& hostname,
                             const std::string& instance_name,
                             uint16_t http_port,
                             const NodeIdentity& identity) {
    ESP_RETURN_ON_ERROR(mdns_init(), "mdns", "init");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(hostname.c_str()), "mdns", "hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set(instance_name.c_str()), "mdns", "instance");

    // Generic HTTP discovery
    (void)mdns_service_add(
        instance_name.c_str(),
        "_http",
        "_tcp",
        http_port,
        nullptr,
        0
    );

    // Dedicated LumiGrid Zeroconf service
    mdns_txt_item_t txt_items[] = {
        {"id", identity.id},
        {"family", identity.family},
        {"model", identity.model},
        {"fw", identity.fw_version},
        {"hw", identity.hw_revision},
        {"api", "1"},
        {"ws", "2"},
        {"auth", "1"}
    };

    return mdns_service_add(
        instance_name.c_str(),
        "_lumigrid",
        "_tcp",
        http_port,
        txt_items,
        sizeof(txt_items) / sizeof(txt_items[0])
    );
}

esp_err_t MdnsService::stop() {
    (void)mdns_service_remove("_lumigrid", "_tcp");
    (void)mdns_service_remove("_http", "_tcp");
    mdns_free();
    return ESP_OK;
}

} // namespace lg
