// components/signage/include/OutputScheduler.hpp
#pragma once
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"
#include "OutputController.hpp"
#include "esp_err.h"

namespace lg {

class OutputScheduler {
public:
    static esp_err_t schedule(
        const RenderFrame& frame,
        const DeviceConfig& device_cfg,
        OutputController& controller,
        const OutputPowerRequest* power_request = nullptr
    );
};

} // namespace lg
