#pragma once
#include <cstdint>
#include "OutputCapabilities.hpp"
#include "SignageTypes.hpp"

namespace lg {

struct CapabilitiesManifest {
    static constexpr uint8_t kSchema = 1;
    static constexpr uint8_t kPixelLanes = OutputCapabilities::kRmtLanes;
    static constexpr uint8_t kPwmChannels = OutputCapabilities::kPca9685Channels;
    static constexpr uint8_t kZones = 8;
    static constexpr uint8_t kLayersPerZone = 3;
    static constexpr uint8_t kPresets = kMaxPresets;
    static constexpr uint8_t kTimelineSteps = 16;

    // Feature flags
    static constexpr bool kFeatureAddressable = true;
    static constexpr bool kFeaturePwm = true;
    static constexpr bool kFeatureArtnet = true;
    static constexpr bool kFeatureSacn = true;
    static constexpr bool kFeatureSync = true;
    static constexpr bool kFeatureMqtt = true;
    static constexpr bool kFeatureFactory = true;
    static constexpr bool kFeatureOta = true;
    static constexpr bool kFeatureSignedOta = true;
    static constexpr bool kFeatureBackup = true;

    // Protocol versions
    static constexpr uint8_t kProtocolHttpApi = 1;
    static constexpr uint8_t kProtocolWebsocket = 2;
    static constexpr uint8_t kProtocolSync = 1;
};

} // namespace lg
