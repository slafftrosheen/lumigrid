#pragma once
#include <cstdint>

namespace lg {

enum class FaultCode : uint16_t {
    None = 0,
    Pca9685NotDetected,
    I2cBusError,
    InvalidConfig,
    PixelTransportError,
    OutputDeadlineMissed,
    TaskWatchdogWarning,
    ArtnetSourceTimeout,
    MqttDisconnected,
    FilesystemMountFailed,
    OtaHealthCheckFailed
};

struct Fault {
    FaultCode code = FaultCode::None;
    uint32_t occurred_at_ms = 0;
    uint32_t count = 0;
    bool latched = false;
};

} // namespace lg
