// components/config/include/ConfigApplyPlan.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "SignageTypes.hpp"

namespace lg {

enum class ConfigApplyImpact : uint32_t {
    None              = 0,
    Metadata          = 1 << 0,
    LightingHot       = 1 << 1,
    OutputReconfigure = 1 << 2,
    DmxReconfigure    = 1 << 3,
    SchedulerRestart  = 1 << 4,
    SyncRestart       = 1 << 5,
    MqttRestart       = 1 << 6,
};

inline ConfigApplyImpact operator|(ConfigApplyImpact a, ConfigApplyImpact b) {
    return static_cast<ConfigApplyImpact>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ConfigApplyImpact& operator|=(ConfigApplyImpact& a, ConfigApplyImpact b) {
    a = a | b;
    return a;
}
inline ConfigApplyImpact operator&(ConfigApplyImpact a, ConfigApplyImpact b) {
    return static_cast<ConfigApplyImpact>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool has_impact(ConfigApplyImpact mask, ConfigApplyImpact flag) {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0;
}

struct ConfigApplyPlan {
    ConfigApplyImpact impact = ConfigApplyImpact::None;
    bool hot_apply = true;
    bool output_restart = false;
    bool dmx_restart = false;
    bool sync_restart = false;
    bool scheduler_restart = false;
    bool mqtt_restart = false;
    bool reboot_required = false;

    bool has_impact(ConfigApplyImpact flag) const { return lg::has_impact(impact, flag); }
    bool requires_output_reconfigure() const { return output_restart; }
    bool requires_lighting_hot() const { return lg::has_impact(impact, ConfigApplyImpact::LightingHot); }
    bool requires_dmx_reconfigure() const { return dmx_restart; }
    bool requires_sync_restart() const { return sync_restart; }
    bool requires_scheduler_restart() const { return scheduler_restart; }
    bool requires_mqtt_restart() const { return mqtt_restart; }
};

ConfigApplyPlan diff_device_config(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg);

} // namespace lg
