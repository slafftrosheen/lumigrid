// components/signage/include/OutputScheduler.hpp
#pragma once
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"
#include "ControlSourceManager.hpp"

namespace lg {

class OutputScheduler {
public:
    static void schedule(
        const RenderFrame& frame,
        const DeviceConfig& device_cfg,
        ControlSourceManager& source_manager,
        ControlSource source
    );
};

} // namespace lg
