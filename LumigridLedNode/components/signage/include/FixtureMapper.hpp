// components/signage/include/FixtureMapper.hpp
#pragma once
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

class FixtureMapper {
public:
    // Maps the composited zone frame to a final physical fixture frame,
    // applying white balance, global brightness, and gamma correction.
    static void map(
        FixtureFrame& dest,
        const FixtureFrame& src,
        const FixtureConfig& fixture,
        uint8_t global_brightness
    );
};

} // namespace lg
