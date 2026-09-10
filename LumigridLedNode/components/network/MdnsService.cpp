// components/network/MdnsService.cpp
#include "MdnsService.hpp"

#include "esp_check.h"
#include "mdns.h"

namespace lg {

esp_err_t MdnsService::start(const std::string& hostname,
                             const std::string& instance_name,
                             uint16_t http_port) {
    ESP_RETURN_ON_ERROR(mdns_init(), "mdns", "init");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(hostname.c_str()), "mdns", "hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set(instance_name.c_str()), "mdns", "instance");

    return mdns_service_add(
        instance_name.c_str(),
        "_http",
        "_tcp",
        http_port,
        nullptr,
        0
    );
}

esp_err_t MdnsService::stop() {
    mdns_free();
    return ESP_OK;
}

} // namespace lg
