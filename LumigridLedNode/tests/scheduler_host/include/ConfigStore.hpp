#pragma once
#include "SignageTypes.hpp"
#include "esp_err.h"
#include <array>
#include <functional>
#include "host.hpp"

namespace lg {
// Persistence is a recording host boundary; playback and transport code are real.
struct ConfigStore {
    std::array<CueList, kMaxCueLists> lists{};
    std::array<bool, kMaxCueLists> present{};
    std::array<bool, kMaxPresets> scenes{};
    Sequence sequence{};
    esp_err_t sequence_result = ESP_OK;
    std::function<void()> load_sequence_hook;
    bool io_inside_critical = false;

    ConfigStore() { scenes.fill(true); }
    esp_err_t load_cue_list(uint8_t index, CueList& out) {
        if (index >= kMaxCueLists || !present[index]) return ESP_ERR_NOT_FOUND;
        out = lists[index];
        return ESP_OK;
    }
    bool scene_exists(uint8_t index) { return index < scenes.size() && scenes[index]; }
    esp_err_t load_sequence(Sequence& out) {
        io_inside_critical |= host::critical_depth != 0;
        if (load_sequence_hook) load_sequence_hook();
        out = sequence; return sequence_result;
    }
    esp_err_t load_scene(uint8_t index, SceneConfig& out) {
        if (!scene_exists(index)) return ESP_ERR_NOT_FOUND;
        out = SceneConfig{};
        out.global_brightness = static_cast<uint8_t>(100 + index);
        return ESP_OK;
    }
    esp_err_t load_calendar_schedule(CalendarSchedule& out) {
        out = CalendarSchedule{}; return ESP_OK;
    }
};
}
