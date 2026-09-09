// components/signage/include/EffectRenderer.hpp
#pragma once
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

class EffectRenderer {
public:
    // Render a single effect layer to the layer frame buffer
    static void render_layer(
        FixtureFrame& out_frame,
        const LayerConfig& layer,
        uint16_t num_pixels,
        uint32_t elapsed_ms
    );
};

} // namespace lg
