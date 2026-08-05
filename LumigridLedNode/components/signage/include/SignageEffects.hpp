// components/signage/include/SignageEffects.hpp
#pragma once
#include <cstdint>
#include "SignageTypes.hpp"
#include "OutputController.hpp"

namespace lg {

/// Renders a fixture's zone content, evaluating layers bottom-up and applying blend modes.
void render_fixture_zone(
    OutputController& output,
    const FixtureConfig& fixture,
    const PhysicalOutput* physical_outputs,
    const ZoneContent& zone,
    uint8_t global_brightness,
    uint32_t elapsed_ms
);

/// Returns whether a scene-level modifier permits the current frame to render.
bool scene_is_visible(
    const SceneConfig& scene,
    uint32_t elapsed_ms,
    uint16_t sequencer_beat_ms
);

} // namespace lg
