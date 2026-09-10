// components/network/include/MdnsService.hpp
#pragma once
#include <string>
#include "esp_err.h"

namespace lg {

class MdnsService {
public:
    esp_err_t start(const std::string& hostname,
                    const std::string& instance_name,
                    uint16_t http_port);
    esp_err_t stop();
};

} // namespace lg
