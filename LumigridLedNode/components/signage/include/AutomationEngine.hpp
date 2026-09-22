// components/signage/include/AutomationEngine.hpp
#pragma once
#include <cstdint>
#include "SignageTypes.hpp"
#include "SceneTransition.hpp"

namespace lg {

class AutomationEngine {
public:
    static uint16_t sample_track(const AutomationTrack& track, uint32_t elapsed_ms);
    static void apply(SceneConfig& sample, const SceneMotionConfig& motion, uint32_t scene_elapsed_ms);
};

} // namespace lg
