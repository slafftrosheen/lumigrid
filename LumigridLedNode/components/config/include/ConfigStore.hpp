// components/config/include/ConfigStore.hpp
#pragma once
#include "SignageTypes.hpp"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

namespace lg {

// ── Legacy Schema v1 Definitions for Deterministic Migration (Item 16) ──
struct PhysicalOutputV1 {
    OutputType type = OutputType::RMT;
    uint8_t hardware_id = 0;
    uint8_t gpio_pin = 0;
    bool enabled = true;
};

struct FixtureConfigV1 {
    FixtureProfile profile = FixtureProfile::WS2812;
    PixelOrder pixel_order = PixelOrder::GRB;
    uint8_t physical_output_index[4] = {};
    uint16_t num_pixels = 0;
    uint8_t max_brightness = 255;
    uint8_t white_balance[3] = {255, 255, 255};
    bool gamma_correction = true;
    uint16_t max_milliwatts = 0;
};

struct ZoneConfigV1 {
    char name[24] = "";
    ZoneRole role = ZoneRole::Unassigned;
    uint32_t fixture_mask = 0;
};

struct ProtocolConfigV1 {
    bool      artnet_enabled       = true;
    bool      sacn_enabled         = true;
    uint16_t  universe_start       = 0;
    MergeMode merge_mode           = MergeMode::HTP;
    uint16_t  stream_timeout_ms    = 2000;
};

struct DeviceConfigV1 {
    char     device_name[32]        = "lumigrid";
    uint8_t  active_preset_index    = 0;
    uint8_t  fallback_preset_index  = 0;
    uint16_t watchdog_timeout_s     = 60;
    uint16_t max_power_mw           = 0;
    uint8_t  gamma_x10              = 22;
    bool     soft_start_enabled     = true;
    uint16_t soft_start_ms          = 500;
    bool     mqtt_enabled           = false;
    char     mqtt_broker[64]        = {};
    uint16_t mqtt_port              = 1883;
    char     timezone[48]           = "UTC0";
    bool     sync_enabled           = false;
    uint8_t  sync_role              = 0;
    uint32_t sync_token             = 0;
    
    char     admin_token[32]        = "lumigrid_admin";
    char     installer_token[32]    = "lumigrid_install";
    char     operator_token[32]     = "lumigrid_op";

    char     serial_number[32]      = "";
    char     hardware_revision[16]  = "v1.0";
    char     site_label[64]         = "";
    char     group_label[64]        = "";
    
    PhysicalOutputV1 physical_outputs[16] = {};
    FixtureConfigV1  fixtures[16]          = {};
    ZoneConfigV1     zones[8]              = {};
    ProtocolConfigV1 protocol              = {};

    uint32_t checksum                      = 0;
};

/// NVS-backed persistent storage for presets, device config, and schedules.
class ConfigStore {
public:
    esp_err_t init();

    // ── Scenes (Product Grade) ──
    esp_err_t save_scene(uint8_t index, const SceneConfig& scene);
    esp_err_t load_scene(uint8_t index, SceneConfig& scene);
    esp_err_t delete_scene(uint8_t index);
    uint8_t   scene_count();

    // ── Device config ──
    esp_err_t save_config(const DeviceConfig& config);
    esp_err_t load_config(DeviceConfig& config);

    struct ConfigValidationResult {
        bool valid = true;
        uint32_t error_count = 0;
        char first_error[128] = "";
    };

    /// Calculates required DMX channel span for a fixture (Batch 5 Item 9)
    static bool calculate_dmx_span(const FixtureConfig& fixture, uint32_t& required_channels);

    /// Determines if a fixture is configured vs unused/disabled (Batch 6 Item 10)
    static bool is_fixture_configured(const FixtureConfig& fixture, const DeviceConfig& config);

    /// Pure configuration validator (Batch 4, 5, 6)
    static esp_err_t validate_config(const DeviceConfig& config, ConfigValidationResult* out_result = nullptr);

    // Explicit Schema v1 -> v2 Migration (Item 16, Batch 3 Item 11)
    static bool migrate_v1_to_v2(const DeviceConfigV1& v1, DeviceConfig& v2, uint32_t* out_warning_count = nullptr);

    // ── Sequences ──
    esp_err_t save_sequence(const Sequence& seq);
    esp_err_t load_sequence(Sequence& seq);

    // ── Calendar scheduling ──
    esp_err_t save_calendar_schedule(const CalendarSchedule& schedule);
    esp_err_t load_calendar_schedule(CalendarSchedule& schedule);

    // ── Active state (power-loss recovery) ──
    esp_err_t save_active_state(uint8_t preset_index);
    esp_err_t load_active_state(uint8_t& preset_index);

private:
    nvs_handle_t handle_ = 0;

    /// Creates a default "Warm White" preset if no presets exist.
    void ensure_defaults();
};

} // namespace lg
