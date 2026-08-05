// components/config/ConfigStore.cpp
#include "ConfigStore.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>
#include <memory>
#include "esp_rom_crc.h"

static const char* TAG = "config";

namespace lg {
namespace {

void make_key(char* buf, size_t buflen, const char* prefix, uint8_t index) {
    snprintf(buf, buflen, "%s_%u", prefix, index);
}

uint32_t calc_scene_crc(const SceneConfig& sc) {
    return esp_rom_crc32_le(0, (const uint8_t*)&sc, sizeof(SceneConfig) - sizeof(uint32_t));
}

uint32_t calc_device_crc(const DeviceConfig& dc) {
    return esp_rom_crc32_le(0, (const uint8_t*)&dc, sizeof(DeviceConfig) - sizeof(uint32_t));
}

uint32_t calc_seq_crc(const Sequence& s) {
    return esp_rom_crc32_le(0, (const uint8_t*)&s, sizeof(Sequence) - sizeof(uint32_t));
}

uint32_t calc_calendar_crc(const CalendarSchedule& c) {
    return esp_rom_crc32_le(0, (const uint8_t*)&c, sizeof(CalendarSchedule) - sizeof(uint32_t));
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
    ESP_LOGI(TAG, "ConfigStore ready (%u scenes)", scene_count());
    return ESP_OK;
}


// ── Scenes (Product Grade) ──────────────────────────────────────────

esp_err_t ConfigStore::save_scene(uint8_t index, const SceneConfig& scene) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    auto to_save = std::make_unique<SceneConfig>(scene);
    to_save->checksum = calc_scene_crc(*to_save);

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    // Save backup of current
    auto current = std::make_unique<SceneConfig>();
    if (load_scene(index, *current) == ESP_OK) {
        char bak_key[16];
        snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
        nvs_set_blob(handle_, bak_key, current.get(), sizeof(SceneConfig));
    }

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, key, to_save.get(), sizeof(SceneConfig)),
        TAG, "save scene"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_scene(uint8_t index, SceneConfig& scene) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    auto load_from_key = [&](const char* k, SceneConfig& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len == sizeof(SceneConfig)) {
                auto temp = std::make_unique<SceneConfig>();
                err = nvs_get_blob(handle_, k, temp.get(), &len);
                if (err == ESP_OK) {
                    if (temp->checksum != calc_scene_crc(*temp)) {
                        ESP_LOGW(TAG, "Scene %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    out = *temp;
                }
            } else if (len == 128) {
                // Legacy v0 SceneConfig (128 bytes)
                struct OldZoneContent {
                    uint8_t effect = 0;
                    BrandColor primary_color = {255, 255, 255};
                    BrandColor secondary_color = {0, 0, 0};
                    uint16_t speed_ms = 2000;
                    uint8_t fx_param = 0;
                    uint8_t palette_id = 0;
                };
                struct OldSceneConfig {
                    char name[24] = "";
                    OldZoneContent zones[8] = {};
                    uint8_t global_brightness = 128;
                    uint16_t transition_ms = 1000;
                    uint32_t checksum = 0;
                };
                
                auto old = std::make_unique<OldSceneConfig>();
                err = nvs_get_blob(handle_, k, old.get(), &len);
                if (err == ESP_OK) {
                    // Check legacy CRC (using the same offset logic)
                    uint32_t expected_crc = ~0U;
                    expected_crc = esp_rom_crc32_le(expected_crc, reinterpret_cast<const uint8_t*>(old.get()), offsetof(OldSceneConfig, checksum));
                    if (old->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy Scene %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    
                    // Migrate
                    auto migrated = std::make_unique<SceneConfig>();
                    strncpy(migrated->name, old->name, sizeof(migrated->name));
                    migrated->global_brightness = old->global_brightness;
                    migrated->transition_ms = old->transition_ms;
                    
                    for (int i = 0; i < 8; ++i) {
                        migrated->zones[i].layers[0].effect = old->zones[i].effect;
                        migrated->zones[i].layers[0].primary_color = old->zones[i].primary_color;
                        migrated->zones[i].layers[0].secondary_color = old->zones[i].secondary_color;
                        migrated->zones[i].layers[0].speed_ms = old->zones[i].speed_ms;
                        migrated->zones[i].layers[0].fx_param = old->zones[i].fx_param;
                        migrated->zones[i].layers[0].palette_id = old->zones[i].palette_id;
                        migrated->zones[i].layers[0].opacity = 255;
                        migrated->zones[i].layers[0].blend_mode = BlendMode::Normal;
                        migrated->zones[i].layers[0].enabled = true;
                    }
                    
                    migrated->checksum = calc_scene_crc(*migrated);
                    out = *migrated;
                    ESP_LOGI(TAG, "Migrated legacy scene %s", k);
                }
            } else {
                err = ESP_ERR_NVS_INVALID_LENGTH;
            }
        }
        return err;
    };

    esp_err_t err = load_from_key(key, scene);
    if (err == ESP_ERR_NVS_INVALID_STATE) {
        // Try backup
        char bak_key[16];
        snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
        ESP_LOGW(TAG, "Restoring scene %u from backup slot", index);
        err = load_from_key(bak_key, scene);
        if (err == ESP_OK) {
            // Restore primary
            nvs_set_blob(handle_, key, &scene, sizeof(SceneConfig));
            nvs_commit(handle_);
        }
    }
    
    return err;
}

esp_err_t ConfigStore::delete_scene(uint8_t index) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    esp_err_t err = nvs_erase_key(handle_, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "delete scene");
    return nvs_commit(handle_);
}

uint8_t ConfigStore::scene_count() {
    uint8_t count = 0;
    auto tmp = std::make_unique<SceneConfig>();
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        if (load_scene(i, *tmp) == ESP_OK) ++count;
    }
    return count;
}

// ── Device config ───────────────────────────────────────────────────

esp_err_t ConfigStore::save_config(const DeviceConfig& config) {
    auto to_save = std::make_unique<DeviceConfig>(config);
    to_save->checksum = calc_device_crc(*to_save);

    auto current = std::make_unique<DeviceConfig>();
    if (load_config(*current) == ESP_OK) {
        nvs_set_blob(handle_, "device_cfg_bak", current.get(), sizeof(DeviceConfig));
    }

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "device_cfg", to_save.get(), sizeof(DeviceConfig)),
        TAG, "save config"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_config(DeviceConfig& config) {
    auto load_from_key = [&](const char* k, DeviceConfig& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len <= sizeof(DeviceConfig)) {
                auto temp = std::make_unique<DeviceConfig>();
                err = nvs_get_blob(handle_, k, temp.get(), &len);
                if (err == ESP_OK) {
                    if (temp->checksum != calc_device_crc(*temp)) {
                        ESP_LOGW(TAG, "DeviceConfig %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    out = *temp;
                }
            } else {
                err = ESP_ERR_NVS_INVALID_LENGTH;
            }
        }
        return err;
    };

    esp_err_t err = load_from_key("device_cfg", config);
    
    if (err == ESP_ERR_NVS_INVALID_STATE) {
        ESP_LOGW(TAG, "Restoring DeviceConfig from backup slot");
        err = load_from_key("device_cfg_bak", config);
        if (err == ESP_OK) {
            nvs_set_blob(handle_, "device_cfg", &config, sizeof(DeviceConfig));
            nvs_commit(handle_);
        }
    }

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_STATE) {
        config = DeviceConfig{};
        // Generate CRC for default config
        config.checksum = calc_device_crc(config);
        return ESP_OK;
    }
    return err;
}

// ── Sequences ───────────────────────────────────────────────────────

esp_err_t ConfigStore::save_sequence(const Sequence& seq) {
    auto to_save = std::make_unique<Sequence>(seq);
    to_save->checksum = calc_seq_crc(*to_save);

    auto current = std::make_unique<Sequence>();
    if (load_sequence(*current) == ESP_OK) {
        nvs_set_blob(handle_, "seq_main_bak", current.get(), sizeof(Sequence));
    }

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "seq_main", to_save.get(), sizeof(Sequence)),
        TAG, "save seq"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_sequence(Sequence& seq) {
    auto load_from_key = [&](const char* k, Sequence& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len <= sizeof(Sequence)) {
                auto temp = std::make_unique<Sequence>();
                err = nvs_get_blob(handle_, k, temp.get(), &len);
                if (err == ESP_OK) {
                    if (temp->checksum != calc_seq_crc(*temp)) {
                        ESP_LOGW(TAG, "Sequence %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    out = *temp;
                }
            } else {
                err = ESP_ERR_NVS_INVALID_LENGTH;
            }
        }
        return err;
    };

    esp_err_t err = load_from_key("seq_main", seq);
    if (err == ESP_ERR_NVS_INVALID_STATE) {
        ESP_LOGW(TAG, "Restoring Sequence from backup slot");
        err = load_from_key("seq_main_bak", seq);
        if (err == ESP_OK) {
            nvs_set_blob(handle_, "seq_main", &seq, sizeof(Sequence));
            nvs_commit(handle_);
        }
    }

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_STATE) {
        seq = Sequence{};
        seq.checksum = calc_seq_crc(seq);
        return ESP_OK;
    }
    return err;
}

// ── Calendar scheduling ────────────────────────────────────────────────

esp_err_t ConfigStore::save_calendar_schedule(const CalendarSchedule& schedule) {
    auto to_save = std::make_unique<CalendarSchedule>(schedule);
    to_save->checksum = calc_calendar_crc(*to_save);

    auto current = std::make_unique<CalendarSchedule>();
    if (load_calendar_schedule(*current) == ESP_OK) {
        nvs_set_blob(handle_, "calendar_bak", current.get(), sizeof(CalendarSchedule));
    }

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "calendar", to_save.get(), sizeof(CalendarSchedule)),
        TAG, "save calendar"
    );
    return nvs_commit(handle_);
}

esp_err_t ConfigStore::load_calendar_schedule(CalendarSchedule& schedule) {
    auto load_from_key = [&](const char* k, CalendarSchedule& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len <= sizeof(CalendarSchedule)) {
                auto temp = std::make_unique<CalendarSchedule>();
                err = nvs_get_blob(handle_, k, temp.get(), &len);
                if (err == ESP_OK) {
                    if (temp->checksum != calc_calendar_crc(*temp)) {
                        ESP_LOGW(TAG, "Calendar %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    out = *temp;
                }
            } else {
                err = ESP_ERR_NVS_INVALID_LENGTH;
            }
        }
        return err;
    };

    esp_err_t err = load_from_key("calendar", schedule);
    if (err == ESP_ERR_NVS_INVALID_STATE) {
        ESP_LOGW(TAG, "Restoring Calendar from backup slot");
        err = load_from_key("calendar_bak", schedule);
        if (err == ESP_OK) {
            nvs_set_blob(handle_, "calendar", &schedule, sizeof(CalendarSchedule));
            nvs_commit(handle_);
        }
    }

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_STATE) {
        schedule = CalendarSchedule{};
        schedule.checksum = calc_calendar_crc(schedule);
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
    auto test = std::make_unique<SceneConfig>();
    if (load_scene(0, *test) == ESP_OK) return; // Already has scenes

    ESP_LOGI(TAG, "No scenes found, creating defaults...");

    // ── Product-Grade Default Scenes ──
    auto s_open = std::make_unique<SceneConfig>();
    strncpy(s_open->name, "Store Open", sizeof(s_open->name));
    s_open->global_brightness = 180;
    for (uint8_t i = 0; i < 8; ++i) {
        s_open->zones[i].layers[0].effect = static_cast<uint8_t>(SignageEffect::Solid);
        s_open->zones[i].layers[0].primary_color = {255, 200, 150};
        s_open->zones[i].layers[0].enabled = true;
        s_open->zones[i].layers[0].opacity = 255;
    }
    save_scene(0, *s_open);

    auto s_night = std::make_unique<SceneConfig>();
    strncpy(s_night->name, "Night Mode", sizeof(s_night->name));
    s_night->global_brightness = 40;
    for (uint8_t i = 0; i < 8; ++i) {
        s_night->zones[i].layers[0].effect = static_cast<uint8_t>(SignageEffect::Breathe);
        s_night->zones[i].layers[0].primary_color = {20, 40, 120};
        s_night->zones[i].layers[0].enabled = true;
        s_night->zones[i].layers[0].opacity = 255;
    }
    save_scene(1, *s_night);
}

} // namespace lg
