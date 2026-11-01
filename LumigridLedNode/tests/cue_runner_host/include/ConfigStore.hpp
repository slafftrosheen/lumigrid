#pragma once
#include "SignageTypes.hpp"
#include "esp_err.h"
#include <array>

namespace lg {
// Persistence is a host boundary; the CueRunner and its data types are real.
struct ConfigStore {
    std::array<CueList, kMaxCueLists> lists{};
    std::array<bool, kMaxCueLists> present{};
    std::array<bool, 16> scenes{};
    Sequence sequence{};
    esp_err_t sequence_result = ESP_OK;

    ConfigStore() { scenes.fill(true); }
    esp_err_t load_cue_list(uint8_t index, CueList& out) {
        if (index >= kMaxCueLists || !present[index]) return ESP_ERR_NOT_FOUND;
        out = lists[index];
        return ESP_OK;
    }
    bool scene_exists(uint8_t index) { return index < scenes.size() && scenes[index]; }
    esp_err_t load_sequence(Sequence& out) { out = sequence; return sequence_result; }
};
}
