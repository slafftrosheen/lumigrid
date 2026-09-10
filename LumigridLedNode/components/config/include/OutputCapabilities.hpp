#pragma once
#include <cstdint>

namespace lg {

/// Authoritative hardware output capabilities (single source of truth).
/// Owned by output hardware specifications, shared with configuration schemas.
struct OutputCapabilities {
    static constexpr uint8_t kRmtLanes = 8;
    static constexpr uint8_t kPca9685Channels = 8;
};

} // namespace lg
