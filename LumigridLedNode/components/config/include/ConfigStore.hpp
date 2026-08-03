// components/config/include/ConfigStore.hpp
#pragma once
#include "SignageTypes.hpp"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

namespace lg {

/// NVS-backed persistent storage for presets, device config, and schedules.
class ConfigStore {
public:
    esp_err_t init();

    // ── Presets ──
    esp_err_t save_preset(uint8_t index, const BrandPreset& preset);
    esp_err_t load_preset(uint8_t index, BrandPreset& preset);
    esp_err_t delete_preset(uint8_t index);
    uint8_t   preset_count();

    // ── Device config ──
    esp_err_t save_config(const DeviceConfig& config);
    esp_err_t load_config(DeviceConfig& config);

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
