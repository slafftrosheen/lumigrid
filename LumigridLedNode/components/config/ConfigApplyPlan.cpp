// components/config/ConfigApplyPlan.cpp
#include "ConfigApplyPlan.hpp"

namespace lg {

ConfigApplyPlan diff_device_config(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg) {
    ConfigApplyPlan plan{};

    // 1. Metadata
    if (strcmp(old_cfg.device_name, new_cfg.device_name) != 0 ||
        strcmp(old_cfg.serial_number, new_cfg.serial_number) != 0 ||
        strcmp(old_cfg.hardware_revision, new_cfg.hardware_revision) != 0 ||
        strcmp(old_cfg.site_label, new_cfg.site_label) != 0 ||
        strcmp(old_cfg.group_label, new_cfg.group_label) != 0) {
        plan.impact |= ConfigApplyImpact::Metadata;
    }

    // 2. Lighting Hot (power, gamma, soft-start, watchdog, presets)
    if (old_cfg.max_power_mw != new_cfg.max_power_mw ||
        old_cfg.gamma_x10 != new_cfg.gamma_x10 ||
        old_cfg.soft_start_enabled != new_cfg.soft_start_enabled ||
        old_cfg.soft_start_ms != new_cfg.soft_start_ms ||
        old_cfg.watchdog_timeout_s != new_cfg.watchdog_timeout_s ||
        old_cfg.active_preset_index != new_cfg.active_preset_index ||
        old_cfg.fallback_preset_index != new_cfg.fallback_preset_index) {
        plan.impact |= ConfigApplyImpact::LightingHot;
    }

    // Check channel differences
    for (uint8_t c = 0; c < 16; ++c) {
        const auto& och = old_cfg.channels[c];
        const auto& nch = new_cfg.channels[c];

        // Hardware topology changes: enabled, or addressable parameters on A1..A8
        if (och.enabled != nch.enabled) {
            plan.impact |= ConfigApplyImpact::OutputReconfigure;
        }
        if (c < 8) { // A1..A8
            if (och.addressable_type != nch.addressable_type ||
                och.pixel_order != nch.pixel_order ||
                och.pixel_count != nch.pixel_count) {
                plan.impact |= ConfigApplyImpact::OutputReconfigure;
            }
        }

        // Lighting parameters: brightness, gamma correction, max mw
        if (och.max_brightness != nch.max_brightness ||
            och.gamma_correction != nch.gamma_correction ||
            och.max_milliwatts != nch.max_milliwatts ||
            och.rated_full_power_mw != nch.rated_full_power_mw ||
            och.channel_limit_mw != nch.channel_limit_mw ||
            strcmp(och.name, nch.name) != 0) {
            plan.impact |= ConfigApplyImpact::LightingHot;
        }

        // DMX channel mapping
        if (och.dmx_universe != nch.dmx_universe ||
            och.dmx_start_channel != nch.dmx_start_channel ||
            och.dmx_mode != nch.dmx_mode) {
            plan.impact |= ConfigApplyImpact::DmxReconfigure;
        }
    }

    // Check power supply differences
    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& os = old_cfg.power_supplies[s];
        const auto& ns = new_cfg.power_supplies[s];
        if (os.enabled != ns.enabled ||
            os.voltage_mv != ns.voltage_mv ||
            os.rated_current_ma != ns.rated_current_ma ||
            os.usable_capacity_permille != ns.usable_capacity_permille ||
            os.channel_mask != ns.channel_mask) {
            plan.impact |= ConfigApplyImpact::LightingHot;
        }
    }

    // Check group differences
    for (uint8_t g = 0; g < 8; ++g) {
        const auto& og = old_cfg.groups[g];
        const auto& ng = new_cfg.groups[g];
        if (strcmp(og.name, ng.name) != 0 ||
            og.channel_mask != ng.channel_mask ||
            og.role != ng.role ||
            og.enabled != ng.enabled) {
            plan.impact |= ConfigApplyImpact::LightingHot;
        }
    }

    // 3. Protocol / DMX reconfigure
    const auto& op = old_cfg.protocol;
    const auto& np = new_cfg.protocol;
    if (op.artnet_enabled != np.artnet_enabled ||
        op.sacn_enabled != np.sacn_enabled ||
        op.universe_start != np.universe_start ||
        op.merge_mode != np.merge_mode ||
        op.stream_timeout_ms != np.stream_timeout_ms ||
        op.live_stream_layer != np.live_stream_layer ||
        op.zone_start_channel != np.zone_start_channel) {
        plan.impact |= ConfigApplyImpact::DmxReconfigure;
    }

    // 4. Scheduler Restart
    if (strcmp(old_cfg.timezone, new_cfg.timezone) != 0) {
        plan.impact |= ConfigApplyImpact::SchedulerRestart;
    }

    // 5. Sync Restart
    if (old_cfg.sync_enabled != new_cfg.sync_enabled ||
        old_cfg.sync_role != new_cfg.sync_role ||
        old_cfg.sync_token != new_cfg.sync_token) {
        plan.impact |= ConfigApplyImpact::SyncRestart;
    }

    // 6. MQTT Restart
    if (old_cfg.mqtt_enabled != new_cfg.mqtt_enabled ||
        strcmp(old_cfg.mqtt_broker, new_cfg.mqtt_broker) != 0 ||
        old_cfg.mqtt_port != new_cfg.mqtt_port) {
        plan.impact |= ConfigApplyImpact::MqttRestart;
    }

    // Set convenience flags
    plan.output_restart = has_impact(plan.impact, ConfigApplyImpact::OutputReconfigure);
    plan.dmx_restart = has_impact(plan.impact, ConfigApplyImpact::DmxReconfigure);
    plan.sync_restart = has_impact(plan.impact, ConfigApplyImpact::SyncRestart);
    plan.scheduler_restart = has_impact(plan.impact, ConfigApplyImpact::SchedulerRestart);
    plan.mqtt_restart = has_impact(plan.impact, ConfigApplyImpact::MqttRestart);
    plan.hot_apply = !plan.output_restart;
    plan.reboot_required = false;

    return plan;
}

} // namespace lg
