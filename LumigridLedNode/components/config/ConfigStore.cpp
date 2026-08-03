// components/config/ConfigStore.cpp
#include "ConfigStore.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "config";

namespace lg {
namespace {

void make_key(char* buf, size_t buflen, const char* prefix, uint8_t index) {
    snprintf(buf, buflen, "%s_%u", prefix, index);
}

} // namespace

esp_err_t ConfigStore::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition full or outdated, erasing...");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "flash init");
    ESP_RETURN_ON_ERROR(nvs_open("lumigrid", NVS_READWRITE, &handle_), TAG, "open");

    ensure_defaults();
    ESP_LOGI(TAG, "ConfigStore ready (%u presets)", preset_count());
    return ESP_OK;
}

// ── Presets ──────────────────────────────────────────────────────────

esp_err_t ConfigStore::save_preset(uint8_t index, const BrandPreset& preset) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "preset", index);

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, key, &preset, sizeof(BrandPreset)),
        TAG, "save preset"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_preset(uint8_t index, BrandPreset& preset) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "preset", index);

    size_t len = sizeof(BrandPreset);
    return nvs_get_blob(handle_, key, &preset, &len);
}

esp_err_t ConfigStore::delete_preset(uint8_t index) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "preset", index);

    esp_err_t err = nvs_erase_key(handle_, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "delete preset");
    return nvs_commit(handle_);
}

uint8_t ConfigStore::preset_count() {
    uint8_t count = 0;
    BrandPreset tmp;
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        if (load_preset(i, tmp) == ESP_OK) ++count;
    }
    return count;
}

// ── Device config ───────────────────────────────────────────────────

esp_err_t ConfigStore::save_config(const DeviceConfig& config) {
    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "device_cfg", &config, sizeof(DeviceConfig)),
        TAG, "save config"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_config(DeviceConfig& config) {
    size_t len = sizeof(DeviceConfig);
    esp_err_t err = nvs_get_blob(handle_, "device_cfg", &config, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Return defaults
        config = DeviceConfig{};
        return ESP_OK;
    }
    return err;
}

// ── Sequences ───────────────────────────────────────────────────────

esp_err_t ConfigStore::save_sequence(const Sequence& seq) {
    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "seq_main", &seq, sizeof(Sequence)),
        TAG, "save seq"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_sequence(Sequence& seq) {
    size_t len = sizeof(Sequence);
    esp_err_t err = nvs_get_blob(handle_, "seq_main", &seq, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        seq = Sequence{};
        return ESP_OK;
    }
    return err;
}

// ── Active state ────────────────────────────────────────────────────

esp_err_t ConfigStore::save_active_state(uint8_t preset_index) {
    ESP_RETURN_ON_ERROR(
        nvs_set_u8(handle_, "active_st", preset_index),
        TAG, "save active"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_active_state(uint8_t& preset_index) {
    esp_err_t err = nvs_get_u8(handle_, "active_st", &preset_index);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        preset_index = 0;
        return ESP_OK;
    }
    return err;
}

// ── Defaults ────────────────────────────────────────────────────────

void ConfigStore::ensure_defaults() {
    BrandPreset test;
    if (load_preset(0, test) == ESP_OK) return; // Already has presets

    ESP_LOGI(TAG, "No presets found, creating defaults...");

    // Preset 0: "Store Open" — warm white, all channels
    BrandPreset open{};
    open.set_name("Store Open");
    open.brightness = 180;
    open.transition_ms = 1500;
    for (uint8_t i = 0; i < 8; ++i) {
        open.channels[i].pixel_color = {255, 200, 150}; // Warm white
        open.channels[i].pwm_duty = 3000;
        open.channels[i].effect = SignageEffect::Solid;
        open.channels[i].effect_speed_ms = 2000;
        open.channels[i].pixel_enabled = true;
        open.channels[i].pwm_enabled = true;
    }
    save_preset(0, open);

    // Preset 1: "Night Mode" — dim blue accent
    BrandPreset night{};
    night.set_name("Night Mode");
    night.brightness = 40;
    night.transition_ms = 3000;
    for (uint8_t i = 0; i < 8; ++i) {
        night.channels[i].pixel_color = {20, 40, 120};
        night.channels[i].pwm_duty = 400;
        night.channels[i].effect = SignageEffect::Breathe;
        night.channels[i].effect_speed_ms = 4000;
        night.channels[i].pixel_enabled = true;
        night.channels[i].pwm_enabled = true;
    }
    save_preset(1, night);

    // Preset 2: "Blackout" — everything off
    BrandPreset blackout{};
    blackout.set_name("Closed");
    blackout.brightness = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        blackout.channels[i].effect = SignageEffect::Solid;
    }
    save_preset(2, blackout);
}

} // namespace lg
