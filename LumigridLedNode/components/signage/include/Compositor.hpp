// components/signage/include/Compositor.hpp
#pragma once
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

class Compositor {
public:
    // Blends the src layer frame into the dest composite frame.
    static void blend(
        FixtureFrame& dest,
        const FixtureFrame& src,
        BlendMode mode,
        uint8_t opacity,
        uint16_t num_pixels
    );
};

} // namespace lg
