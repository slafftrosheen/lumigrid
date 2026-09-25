// components/network/include/MdnsService.hpp
#pragma once
#include <string>
#include "esp_err.h"
#include "NodeIdentity.hpp"

namespace lg {

class MdnsService {
public:
    esp_err_t start(const std::string& hostname,
                    const std::string& instance_name,
                    uint16_t http_port,
                    const NodeIdentity& identity);
    esp_err_t stop();
};

} // namespace lg
