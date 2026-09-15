// components/config/ConfigStore.cpp
#include "ConfigStore.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <memory>
#include "esp_rom_crc.h"

static const char* TAG = "config";

namespace lg {
namespace {

void safe_strncpy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0) return;
    std::memcpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

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

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, key, to_save.get(), sizeof(SceneConfig)),
        TAG, "save scene primary"
    );
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), TAG, "commit scene primary");

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
    esp_err_t bak_err = nvs_set_blob(handle_, bak_key, to_save.get(), sizeof(SceneConfig));
    if (bak_err != ESP_OK) {
        ESP_LOGW(TAG, "Scene %u backup write failed: %s", index, esp_err_to_name(bak_err));
    } else {
        bak_err = nvs_commit(handle_);
        if (bak_err != ESP_OK) {
            ESP_LOGW(TAG, "Scene %u backup commit failed: %s", index, esp_err_to_name(bak_err));
        }
    }

    return ESP_OK;
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
                    
                    auto migrated = std::make_unique<SceneConfig>();
                    std::memcpy(migrated->name, old->name, sizeof(migrated->name) - 1);
                    migrated->name[sizeof(migrated->name) - 1] = '\0';
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
            esp_err_t restore_err = nvs_set_blob(handle_, key, &scene, sizeof(SceneConfig));
            if (restore_err != ESP_OK) {
                ESP_LOGW(TAG, "Scene %u primary restore write failed: %s", index, esp_err_to_name(restore_err));
            } else {
                restore_err = nvs_commit(handle_);
                if (restore_err != ESP_OK) {
                    ESP_LOGW(TAG, "Scene %u primary restore commit failed: %s", index, esp_err_to_name(restore_err));
                }
            }
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

bool ConfigStore::calculate_dmx_span(const FixtureConfig& fixture, uint32_t& required_channels) {
    if (fixture.dmx_start_channel == 0) {
        required_channels = 0;
        return true;
    }

    if (fixture.dmx_mode == 0) {
        // Wash mode
        switch (fixture.profile) {
            case FixtureProfile::PWM_Single:
                required_channels = 1;
                break;
            case FixtureProfile::PWM_RGB:
            case FixtureProfile::WS2812:
                required_channels = 3;
                break;
            case FixtureProfile::PWM_RGBW:
            case FixtureProfile::SK6812:
                required_channels = 4;
                break;
            default:
                required_channels = 0;
                return false;
        }
    } else if (fixture.dmx_mode == 1) {
        // Direct pixel map mode
        bool is_addressable = (fixture.profile == FixtureProfile::WS2812 || fixture.profile == FixtureProfile::SK6812);
        if (is_addressable && fixture.num_pixels == 0) {
            // Addressable fixture with 0 pixels is disabled/unconfigured
            required_channels = 0;
            return false;
        }
        uint32_t ch_per_pixel = (fixture.profile == FixtureProfile::SK6812 || fixture.profile == FixtureProfile::PWM_RGBW) ? 4 : 3;
        required_channels = static_cast<uint32_t>(fixture.num_pixels) * ch_per_pixel;
    } else {
        // Reject unknown dmx_mode values
        required_channels = 0;
        return false;
    }

    // Check using:
    // start = dmx_start_channel - 1
    // end_exclusive = start + required_channels
    // valid iff: dmx_start_channel >= 1 && end_exclusive <= 512
    uint32_t start = static_cast<uint32_t>(fixture.dmx_start_channel) - 1;
    uint32_t end_exclusive = start + required_channels;

    if (fixture.dmx_start_channel < 1 || end_exclusive > 512) {
        return false;
    }

    return true;
}

bool ConfigStore::is_fixture_configured(const FixtureConfig& fix, const DeviceConfig& cfg) {
    const bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
    const bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                         fix.profile == FixtureProfile::PWM_RGB ||
                         fix.profile == FixtureProfile::PWM_RGBW);
    if (!is_addressable && !is_pwm) {
        return false;
    }
    if (is_addressable) {
        // Addressable fixture: num_pixels == 0 -> disabled/unconfigured (Batch 6 Item 10)
        return fix.num_pixels > 0;
    }
    // PWM fixture: physical outputs determined by profile channel count. num_pixels is irrelevant.
    // Configured if dmx_start_channel > 0 or at least one referenced physical output is valid & enabled.
    if (fix.dmx_start_channel > 0) {
        return true;
    }
    uint8_t channels = (fix.profile == FixtureProfile::PWM_Single) ? 1 :
                       (fix.profile == FixtureProfile::PWM_RGB ? 3 : 4);
    for (uint8_t ch = 0; ch < channels; ++ch) {
        uint8_t p_idx = fix.physical_output_index[ch];
        if (p_idx < 16 && cfg.physical_outputs[p_idx].enabled) {
            return true;
        }
    }
    return false;
}

esp_err_t ConfigStore::validate_config(const DeviceConfig& config, ConfigValidationResult* out_result) {
    ConfigValidationResult res;
    auto add_error = [&](const char* fmt, ...) {
        res.valid = false;
        res.error_count++;
        if (res.first_error[0] == '\0') {
            va_list args;
            va_start(args, fmt);
            vsnprintf(res.first_error, sizeof(res.first_error), fmt, args);
            va_end(args);
        }
    };

    // 1. General device checks
    if (config.gamma_x10 < 10 || config.gamma_x10 > 30) {
        add_error("gamma_x10 (%u) out of sane range [10, 30]", (unsigned)config.gamma_x10);
    }
    if (config.max_power_mw > 65000) {
        add_error("max_power_mw (%u) exceeds sane limit 65000", (unsigned)config.max_power_mw);
    }

    // 2. Protocol configuration
    if (config.protocol.live_stream_layer >= 3) {
        add_error("protocol.live_stream_layer (%u) must be < 3", (unsigned)config.protocol.live_stream_layer);
    }
    if (config.protocol.zone_start_channel > 512) {
        add_error("protocol.zone_start_channel (%u) must be in 0..512 (0=unpatched)", (unsigned)config.protocol.zone_start_channel);
    }
    if (config.protocol.zone_start_channel > 0 && (config.protocol.zone_start_channel + 24 - 1 > 512)) {
        add_error("protocol.zone_start_channel (%u) + 24 channels exceeds universe boundary 512",
                  (unsigned)config.protocol.zone_start_channel);
    }

    // 3. Physical Outputs validation (Batch 6 Items 1 & 2: Enabled/Unused semantics)
    for (uint8_t p = 0; p < 16; ++p) {
        const auto& phys = config.physical_outputs[p];
        if (!phys.enabled) {
            // Output slot is unused; hardware_id / type fields do not need to be valid
            continue;
        }
        if (phys.type == OutputType::RMT) {
            if (phys.hardware_id >= MAX_RMT_LANES) {
                add_error("physical_output[%u] RMT hardware_id (%u) >= %u",
                          p, (unsigned)phys.hardware_id, (unsigned)MAX_RMT_LANES);
            }
        } else if (phys.type == OutputType::PWM) {
            if (phys.hardware_id >= MAX_PWM_CHANNELS) {
                add_error("physical_output[%u] PWM hardware_id (%u) >= %u",
                          p, (unsigned)phys.hardware_id, (unsigned)MAX_PWM_CHANNELS);
            }
        } else {
            add_error("physical_output[%u] unsupported output type (%u)", p, (unsigned)phys.type);
        }
    }

    // 4. Fixture configuration and Anti-Aliasing Topology Validation (Batch 7 Items 1, 2, 3)
    uint16_t rmt_lanes_claimed = 0;
    uint16_t pwm_channels_claimed = 0;

    for (uint8_t f = 0; f < 16; ++f) {
        const auto& fix = config.fixtures[f];

        // An unused fixture with default/stale fields must not fail validation
        if (!is_fixture_configured(fix, config)) {
            continue;
        }

        if (fix.zone_id >= 8) {
            add_error("fixture[%u].zone_id (%u) must be < 8", (unsigned)f, (unsigned)fix.zone_id);
        }

        bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
        bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                       fix.profile == FixtureProfile::PWM_RGB ||
                       fix.profile == FixtureProfile::PWM_RGBW);

        if (is_addressable) {
            if (fix.num_pixels > MAX_PIXELS) {
                add_error("fixture[%u].num_pixels (%u) exceeds MAX_PIXELS (%u)",
                          (unsigned)f, (unsigned)fix.num_pixels, (unsigned)MAX_PIXELS);
            }
            uint8_t p_idx = fix.physical_output_index[0];
            if (p_idx >= 16) {
                add_error("fixture[%u].physical_output_index[0] (%u) must be < 16", (unsigned)f, (unsigned)p_idx);
            } else {
                const auto& phys = config.physical_outputs[p_idx];
                if (!phys.enabled) {
                    add_error("fixture[%u] references disabled physical_output[%u]", (unsigned)f, (unsigned)p_idx);
                }
                if (phys.type != OutputType::RMT) {
                    add_error("fixture[%u] addressable profile requires RMT physical output, got %u",
                              (unsigned)f, (unsigned)phys.type);
                } else if (phys.hardware_id >= MAX_RMT_LANES) {
                    add_error("fixture[%u] RMT hardware_id (%u) >= %u", (unsigned)f, (unsigned)phys.hardware_id, (unsigned)MAX_RMT_LANES);
                } else {
                    if (rmt_lanes_claimed & (1U << phys.hardware_id)) {
                        add_error("fixture[%u] RMT lane %u already claimed by another fixture",
                                  (unsigned)f, (unsigned)phys.hardware_id);
                    }
                    rmt_lanes_claimed |= (1U << phys.hardware_id);
                }
            }
        } else if (is_pwm) {
            uint8_t channels_used = (fix.profile == FixtureProfile::PWM_Single) ? 1 :
                                    (fix.profile == FixtureProfile::PWM_RGB ? 3 : 4);
            for (uint8_t ch = 0; ch < channels_used; ++ch) {
                uint8_t p_idx = fix.physical_output_index[ch];
                if (p_idx >= 16) {
                    add_error("fixture[%u].physical_output_index[%u] (%u) must be < 16", (unsigned)f, (unsigned)ch, (unsigned)p_idx);
                } else {
                    const auto& phys = config.physical_outputs[p_idx];
                    if (!phys.enabled) {
                        add_error("fixture[%u] references disabled physical_output[%u]", (unsigned)f, (unsigned)p_idx);
                    }
                    if (phys.type != OutputType::PWM) {
                        add_error("fixture[%u] PWM profile requires PWM physical output, got %u",
                                  (unsigned)f, (unsigned)phys.type);
                    } else if (phys.hardware_id >= MAX_PWM_CHANNELS) {
                        add_error("fixture[%u] PWM hardware_id (%u) >= %u", (unsigned)f, (unsigned)phys.hardware_id, (unsigned)MAX_PWM_CHANNELS);
                    } else {
                        if (pwm_channels_claimed & (1U << phys.hardware_id)) {
                            add_error("fixture[%u] PWM channel %u already claimed by another fixture",
                                      (unsigned)f, (unsigned)phys.hardware_id);
                        }
                        pwm_channels_claimed |= (1U << phys.hardware_id);
                    }
                }
            }
        } else {
            add_error("fixture[%u] unsupported profile (%u)", (unsigned)f, (unsigned)fix.profile);
        }

        // DMX patching checks for configured fixture (Batch 5 Item 9 & Batch 6 Item 11)
        if (fix.dmx_start_channel > 0) {
            uint32_t req_channels = 0;
            if (!calculate_dmx_span(fix, req_channels)) {
                add_error("fixture[%u] DMX patch invalid (start=%u, mode=%u)",
                          (unsigned)f, (unsigned)fix.dmx_start_channel, (unsigned)fix.dmx_mode);
            }
            if (fix.dmx_universe > 63999) {
                add_error("fixture[%u].dmx_universe (%u) exceeds valid range 63999",
                          (unsigned)f, (unsigned)fix.dmx_universe);
            }
        }
    }

    if (out_result) {
        *out_result = res;
    }

    return res.valid ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t ConfigStore::save_config(const DeviceConfig& config) {
    ConfigValidationResult result;
    esp_err_t val_err = validate_config(config, &result);
    cached_val_res_ = result;
    if (val_err != ESP_OK) {
        persistence_state_ = ConfigPersistenceState::SaveFailed;
        ESP_LOGE(TAG, "Config save rejected due to validation failure (%u error(s)): %s",
                 (unsigned)result.error_count, result.first_error);
        return val_err;
    }

    auto to_save = std::make_unique<DeviceConfig>(config);
    to_save->checksum = calc_device_crc(*to_save);

    // 1. Transactional primary write & commit (Batch 6 Item 18)
    esp_err_t err = nvs_set_blob(handle_, "device_cfg", to_save.get(), sizeof(DeviceConfig));
    if (err != ESP_OK) {
        persistence_state_ = ConfigPersistenceState::SaveFailed;
        primary_valid_ = false;
        ESP_LOGE(TAG, "Failed to write primary device_cfg: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(handle_);
    if (err != ESP_OK) {
        persistence_state_ = ConfigPersistenceState::SaveFailed;
        primary_valid_ = false;
        ESP_LOGE(TAG, "Failed to commit primary device_cfg: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Verify primary read-back and checksum before touching backup
    auto verify_buf = std::make_unique<DeviceConfig>();
    size_t verify_len = sizeof(DeviceConfig);
    err = nvs_get_blob(handle_, "device_cfg", verify_buf.get(), &verify_len);
    if (err != ESP_OK || verify_len != sizeof(DeviceConfig) ||
        verify_buf->checksum != to_save->checksum ||
        calc_device_crc(*verify_buf) != to_save->checksum) {
        persistence_state_ = ConfigPersistenceState::SaveFailed;
        primary_valid_ = false;
        ESP_LOGE(TAG, "Primary device_cfg read-back verification failed! Preserving backup intact.");
        return ESP_ERR_NVS_INVALID_STATE;
    }

    primary_valid_ = true;

    // 3. Primary confirmed valid -> update backup slot (Batch 7 Item 15, Batch 8 Item 13)
    err = nvs_set_blob(handle_, "device_cfg_bak", to_save.get(), sizeof(DeviceConfig));
    if (err != ESP_OK) {
        persistence_state_ = ConfigPersistenceState::PrimaryOnly;
        backup_valid_ = false;
        ESP_LOGW(TAG, "Primary device_cfg committed, but writing backup slot failed: %s (State: PrimaryOnly)", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(handle_);
    if (err != ESP_OK) {
        persistence_state_ = ConfigPersistenceState::PrimaryOnly;
        backup_valid_ = false;
        ESP_LOGW(TAG, "Primary device_cfg committed, but committing backup slot failed: %s (State: PrimaryOnly)", esp_err_to_name(err));
        return err;
    }

    backup_valid_ = true;
    persistence_state_ = ConfigPersistenceState::Synchronized;
    return ESP_OK;
}

bool ConfigStore::migrate_v1_to_v2(const DeviceConfigV1& v1, DeviceConfig& v2, uint32_t* out_warning_count) {
    auto def = std::make_unique<DeviceConfig>();
    v2 = *def;
    v2.schema_version = 2;
    safe_strncpy(v2.device_name, v1.device_name, sizeof(v2.device_name));
    v2.active_preset_index = v1.active_preset_index;
    v2.fallback_preset_index = v1.fallback_preset_index;
    v2.watchdog_timeout_s = v1.watchdog_timeout_s;
    v2.max_power_mw = v1.max_power_mw;
    v2.gamma_x10 = v1.gamma_x10;
    v2.soft_start_enabled = v1.soft_start_enabled;
    v2.soft_start_ms = v1.soft_start_ms;
    v2.mqtt_enabled = v1.mqtt_enabled;
    safe_strncpy(v2.mqtt_broker, v1.mqtt_broker, sizeof(v2.mqtt_broker));
    v2.mqtt_port = v1.mqtt_port;
    safe_strncpy(v2.timezone, v1.timezone, sizeof(v2.timezone));
    v2.sync_enabled = v1.sync_enabled;
    v2.sync_role = v1.sync_role;
    v2.sync_token = v1.sync_token;

    safe_strncpy(v2.admin_token, v1.admin_token, sizeof(v2.admin_token));
    safe_strncpy(v2.installer_token, v1.installer_token, sizeof(v2.installer_token));
    safe_strncpy(v2.operator_token, v1.operator_token, sizeof(v2.operator_token));

    safe_strncpy(v2.serial_number, v1.serial_number, sizeof(v2.serial_number));
    safe_strncpy(v2.hardware_revision, v1.hardware_revision, sizeof(v2.hardware_revision));
    safe_strncpy(v2.site_label, v1.site_label, sizeof(v2.site_label));
    safe_strncpy(v2.group_label, v1.group_label, sizeof(v2.group_label));

    for (size_t i = 0; i < 16; ++i) {
        v2.physical_outputs[i].type = v1.physical_outputs[i].type;
        v2.physical_outputs[i].hardware_id = v1.physical_outputs[i].hardware_id;
        v2.physical_outputs[i].gpio_pin = v1.physical_outputs[i].gpio_pin;
        v2.physical_outputs[i].enabled = v1.physical_outputs[i].enabled;
    }

    for (size_t i = 0; i < 8; ++i) {
        safe_strncpy(v2.zones[i].name, v1.zones[i].name, sizeof(v2.zones[i].name));
        v2.zones[i].role = v1.zones[i].role;
    }

    for (size_t f = 0; f < 16; ++f) {
        v2.fixtures[f].profile = v1.fixtures[f].profile;
        v2.fixtures[f].pixel_order = v1.fixtures[f].pixel_order;
        for (int p = 0; p < 4; ++p) {
            v2.fixtures[f].physical_output_index[p] = v1.fixtures[f].physical_output_index[p];
        }
        v2.fixtures[f].num_pixels = v1.fixtures[f].num_pixels;
        v2.fixtures[f].max_brightness = v1.fixtures[f].max_brightness;
        for (int w = 0; w < 3; ++w) {
            v2.fixtures[f].white_balance[w] = v1.fixtures[f].white_balance[w];
        }
        v2.fixtures[f].gamma_correction = v1.fixtures[f].gamma_correction;
        v2.fixtures[f].max_milliwatts = v1.fixtures[f].max_milliwatts;

        // Count matching zones from legacy v1 fixture_mask (Batch 3 Item 11)
        uint8_t match_count = 0;
        uint8_t first_zone = 0;
        for (uint8_t z = 0; z < 8; ++z) {
            if (v1.zones[z].fixture_mask & (1U << f)) {
                if (match_count == 0) {
                    first_zone = z;
                }
                match_count++;
            }
        }

        if (match_count == 0) {
            v2.fixtures[f].zone_id = 0;
            ESP_LOGW(TAG, "Migration: Fixture %u has 0 zone memberships in legacy config, assigning to zone 0", (unsigned)f);
            if (out_warning_count) (*out_warning_count)++;
        } else if (match_count == 1) {
            v2.fixtures[f].zone_id = first_zone;
        } else {
            v2.fixtures[f].zone_id = first_zone;
            ESP_LOGW(TAG, "Migration: Fixture %u belonged to %u legacy zones (collapsed to first matching zone %u)",
                     (unsigned)f, (unsigned)match_count, (unsigned)first_zone);
            if (out_warning_count) (*out_warning_count)++;
        }

        // Validate fixture pixel limits (Batch 3 Item 10)
        bool is_addressable = (v2.fixtures[f].profile == FixtureProfile::WS2812 || v2.fixtures[f].profile == FixtureProfile::SK6812);
        if (is_addressable && v2.fixtures[f].num_pixels > MAX_PIXELS) {
            ESP_LOGE(TAG, "Migration Error: Fixture %u num_pixels (%u) exceeds MAX_PIXELS (%u). Marking invalid.",
                     (unsigned)f, (unsigned)v2.fixtures[f].num_pixels, (unsigned)MAX_PIXELS);
            v2.fixtures[f].num_pixels = 0;
            if (out_warning_count) (*out_warning_count)++;
        }

        v2.fixtures[f].dmx_universe = 0;
        v2.fixtures[f].dmx_start_channel = 0;
        v2.fixtures[f].dmx_mode = 0;
    }

    v2.protocol.artnet_enabled = v1.protocol.artnet_enabled;
    v2.protocol.sacn_enabled = v1.protocol.sacn_enabled;
    v2.protocol.universe_start = v1.protocol.universe_start;
    v2.protocol.merge_mode = v1.protocol.merge_mode;
    v2.protocol.stream_timeout_ms = v1.protocol.stream_timeout_ms;
    v2.protocol.live_stream_layer = 2;
    v2.protocol.zone_start_channel = 1;

    v2.checksum = calc_device_crc(v2);
    return true;
}

esp_err_t ConfigStore::load_config(DeviceConfig& config) {
    bool needs_persist = false;

    auto load_from_key = [&](const char* k, DeviceConfig& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len == sizeof(DeviceConfig)) {
                auto temp = std::make_unique<DeviceConfig>();
                err = nvs_get_blob(handle_, k, temp.get(), &len);
                if (err == ESP_OK) {
                    if (temp->checksum != calc_device_crc(*temp)) {
                        ESP_LOGW(TAG, "DeviceConfig %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    if (temp->schema_version < 2) {
                        ESP_LOGI(TAG, "Migrating DeviceConfig %s from in-memory schema v%u to v2", k, temp->schema_version);
                        temp->schema_version = 2;
                        temp->checksum = calc_device_crc(*temp);
                        needs_persist = true;
                    }
                    out = *temp;
                }
            } else if (len == sizeof(DeviceConfigV1)) {
                // Real schema migration: decode legacy v1 structure (Item 16)
                auto v1 = std::make_unique<DeviceConfigV1>();
                err = nvs_get_blob(handle_, k, v1.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v1.get(), sizeof(DeviceConfigV1) - sizeof(uint32_t));
                    if (v1->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy DeviceConfigV1 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    ESP_LOGI(TAG, "Explicitly migrating legacy v1 blob %s to v2", k);
                    migrate_v1_to_v2(*v1, out);
                    ConfigValidationResult vres;
                    if (validate_config(out, &vres) != ESP_OK) {
                        ESP_LOGE(TAG, "Migration validation failed (%u errors): %s",
                                 (unsigned)vres.error_count, vres.first_error);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    needs_persist = true;
                }
            } else {
                ESP_LOGW(TAG, "DeviceConfig size mismatch (len=%u, expected=%u or %u)",
                         (unsigned)len, (unsigned)sizeof(DeviceConfig), (unsigned)sizeof(DeviceConfigV1));
                err = ESP_ERR_NVS_INVALID_LENGTH;
            }
        }
        return err;
    };

    esp_err_t err = load_from_key("device_cfg", config);
    if (err == ESP_OK) {
        primary_valid_ = true;
        DeviceConfig bak_cfg;
        esp_err_t bak_err = load_from_key("device_cfg_bak", bak_cfg);
        if (bak_err == ESP_OK && bak_cfg.checksum == config.checksum) {
            backup_valid_ = true;
            persistence_state_ = ConfigPersistenceState::Synchronized;
        } else {
            backup_valid_ = false;
            persistence_state_ = ConfigPersistenceState::PrimaryOnly;
        }
    } else if (err == ESP_ERR_NVS_INVALID_STATE || err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_NVS_NOT_FOUND) {
        primary_valid_ = false;
        ESP_LOGW(TAG, "Restoring DeviceConfig from backup slot");
        err = load_from_key("device_cfg_bak", config);
        if (err == ESP_OK) {
            backup_valid_ = true;
            persistence_state_ = ConfigPersistenceState::PrimaryOnly;
            needs_persist = true;
        } else {
            backup_valid_ = false;
            persistence_state_ = ConfigPersistenceState::RecoveryRequired;
        }
    } else {
        primary_valid_ = false;
        backup_valid_ = false;
        persistence_state_ = ConfigPersistenceState::RecoveryRequired;
    }

    auto generate_token = [](char* out, size_t max_len) {
        uint8_t rand_bytes[8];
        esp_fill_random(rand_bytes, sizeof(rand_bytes));
        snprintf(out, max_len, "%02x%02x%02x%02x%02x%02x%02x%02x",
                 rand_bytes[0], rand_bytes[1], rand_bytes[2], rand_bytes[3],
                 rand_bytes[4], rand_bytes[5], rand_bytes[6], rand_bytes[7]);
    };

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_STATE || err == ESP_ERR_NVS_INVALID_LENGTH) {
        auto def = std::make_unique<DeviceConfig>();
        config = *def;
        generate_token(config.admin_token, sizeof(config.admin_token));
        generate_token(config.installer_token, sizeof(config.installer_token));
        generate_token(config.operator_token, sizeof(config.operator_token));
        
        ESP_LOGW(TAG, "==================================================");
        ESP_LOGW(TAG, "  NEW CREDENTIALS GENERATED");
        ESP_LOGW(TAG, "  Admin Token:     %s", config.admin_token);
        ESP_LOGW(TAG, "  Installer Token: %s", config.installer_token);
        ESP_LOGW(TAG, "  Operator Token:  %s", config.operator_token);
        ESP_LOGW(TAG, "  STORE THESE SECURELY. THEY WILL NOT BE SHOWN AGAIN.");
        ESP_LOGW(TAG, "==================================================");

        config.schema_version = 2;
        config.checksum = calc_device_crc(config);
        save_config(config);
        return ESP_OK;
    } else if (err == ESP_OK) {
        // Rotate any known legacy default credentials still present in stored config
        bool token_rotated = false;
        if (std::strcmp(config.admin_token, "lumigrid_admin") == 0) {
            generate_token(config.admin_token, sizeof(config.admin_token));
            token_rotated = true;
        }
        if (std::strcmp(config.installer_token, "lumigrid_install") == 0) {
            generate_token(config.installer_token, sizeof(config.installer_token));
            token_rotated = true;
        }
        if (std::strcmp(config.operator_token, "lumigrid_op") == 0) {
            generate_token(config.operator_token, sizeof(config.operator_token));
            token_rotated = true;
        }

        if (token_rotated) {
            ESP_LOGW(TAG, "==================================================");
            ESP_LOGW(TAG, "  LEGACY DEFAULT CREDENTIALS DETECTED & ROTATED");
            ESP_LOGW(TAG, "  Admin Token:     %s", config.admin_token);
            ESP_LOGW(TAG, "  Installer Token: %s", config.installer_token);
            ESP_LOGW(TAG, "  Operator Token:  %s", config.operator_token);
            ESP_LOGW(TAG, "  PERSISTING TO NVS. STORE THESE SECURELY.");
            ESP_LOGW(TAG, "==================================================");
            needs_persist = true;
        }

        if (needs_persist) {
            ESP_LOGI(TAG, "Persisting migrated/updated DeviceConfig to NVS");
            esp_err_t s_err = save_config(config);
            if (s_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to persist migrated DeviceConfig to NVS: %s", esp_err_to_name(s_err));
                return s_err;
            }

            // Verify persisted data by reloading it before reporting migration success (Batch 3 Item 11)
            auto verify_cfg = std::make_unique<DeviceConfig>();
            size_t verify_len = sizeof(DeviceConfig);
            esp_err_t v_err = nvs_get_blob(handle_, "device_cfg", verify_cfg.get(), &verify_len);
            if (v_err != ESP_OK || verify_len != sizeof(DeviceConfig) || verify_cfg->checksum != calc_device_crc(*verify_cfg)) {
                ESP_LOGE(TAG, "Verification of persisted migrated config failed!");
                return ESP_ERR_NVS_INVALID_STATE;
            }
            ESP_LOGI(TAG, "Migrated config successfully persisted and verified from NVS");
        }
    }
    if (err == ESP_OK) {
        validate_config(config, &cached_val_res_);
    } else {
        cached_val_res_.valid = false;
        cached_val_res_.error_count = 1;
        safe_strncpy(cached_val_res_.first_error, "Failed to load device config from NVS", sizeof(cached_val_res_.first_error));
    }
    return err;
}

// ── Sequences ───────────────────────────────────────────────────────

esp_err_t ConfigStore::save_sequence(const Sequence& seq) {
    auto to_save = std::make_unique<Sequence>(seq);
    to_save->checksum = calc_seq_crc(*to_save);

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "seq_main", to_save.get(), sizeof(Sequence)),
        TAG, "save seq primary"
    );
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), TAG, "commit seq primary");

    esp_err_t bak_err = nvs_set_blob(handle_, "seq_main_bak", to_save.get(), sizeof(Sequence));
    if (bak_err != ESP_OK) {
        ESP_LOGW(TAG, "Sequence backup write failed: %s", esp_err_to_name(bak_err));
    } else {
        bak_err = nvs_commit(handle_);
        if (bak_err != ESP_OK) {
            ESP_LOGW(TAG, "Sequence backup commit failed: %s", esp_err_to_name(bak_err));
        }
    }

    return ESP_OK;
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
            esp_err_t restore_err = nvs_set_blob(handle_, "seq_main", &seq, sizeof(Sequence));
            if (restore_err != ESP_OK) {
                ESP_LOGW(TAG, "Sequence primary restore write failed: %s", esp_err_to_name(restore_err));
            } else {
                restore_err = nvs_commit(handle_);
                if (restore_err != ESP_OK) {
                    ESP_LOGW(TAG, "Sequence primary restore commit failed: %s", esp_err_to_name(restore_err));
                }
            }
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

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, "calendar", to_save.get(), sizeof(CalendarSchedule)),
        TAG, "save calendar primary"
    );
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), TAG, "commit calendar primary");

    esp_err_t bak_err = nvs_set_blob(handle_, "calendar_bak", to_save.get(), sizeof(CalendarSchedule));
    if (bak_err != ESP_OK) {
        ESP_LOGW(TAG, "Calendar backup write failed: %s", esp_err_to_name(bak_err));
    } else {
        bak_err = nvs_commit(handle_);
        if (bak_err != ESP_OK) {
            ESP_LOGW(TAG, "Calendar backup commit failed: %s", esp_err_to_name(bak_err));
        }
    }

    return ESP_OK;
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
            esp_err_t restore_err = nvs_set_blob(handle_, "calendar", &schedule, sizeof(CalendarSchedule));
            if (restore_err != ESP_OK) {
                ESP_LOGW(TAG, "Calendar primary restore write failed: %s", esp_err_to_name(restore_err));
            } else {
                restore_err = nvs_commit(handle_);
                if (restore_err != ESP_OK) {
                    ESP_LOGW(TAG, "Calendar primary restore commit failed: %s", esp_err_to_name(restore_err));
                }
            }
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
    safe_strncpy(s_open->name, "Store Open", sizeof(s_open->name));
    s_open->global_brightness = 180;
    for (uint8_t i = 0; i < 8; ++i) {
        s_open->zones[i].layers[0].effect = static_cast<uint8_t>(SignageEffect::Solid);
        s_open->zones[i].layers[0].primary_color = {255, 200, 150};
        s_open->zones[i].layers[0].enabled = true;
        s_open->zones[i].layers[0].opacity = 255;
    }
    esp_err_t err = save_scene(0, *s_open);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save default scene 0: %s", esp_err_to_name(err));
    }

    auto s_night = std::make_unique<SceneConfig>();
    safe_strncpy(s_night->name, "Night Mode", sizeof(s_night->name));
    s_night->global_brightness = 40;
    for (uint8_t i = 0; i < 8; ++i) {
        s_night->zones[i].layers[0].effect = static_cast<uint8_t>(SignageEffect::Breathe);
        s_night->zones[i].layers[0].primary_color = {20, 40, 120};
        s_night->zones[i].layers[0].enabled = true;
        s_night->zones[i].layers[0].opacity = 255;
    }
    err = save_scene(1, *s_night);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save default scene 1: %s", esp_err_to_name(err));
    }

    // 🔒 Security Provisioning
    auto dc = std::make_unique<DeviceConfig>();
    if (load_config(*dc) == ESP_OK) {
        if (strcmp(dc->admin_token, "lumigrid_admin") == 0) {
            ESP_LOGI(TAG, "Factory default credentials detected. Provisioning new tokens...");
            
            auto generate_token = [](char* out, size_t max_len) {
                uint8_t rand_bytes[8];
                esp_fill_random(rand_bytes, sizeof(rand_bytes));
                snprintf(out, max_len, "%02x%02x%02x%02x%02x%02x%02x%02x",
                         rand_bytes[0], rand_bytes[1], rand_bytes[2], rand_bytes[3],
                         rand_bytes[4], rand_bytes[5], rand_bytes[6], rand_bytes[7]);
            };

            generate_token(dc->admin_token, sizeof(dc->admin_token));
            generate_token(dc->installer_token, sizeof(dc->installer_token));
            generate_token(dc->operator_token, sizeof(dc->operator_token));

            ESP_LOGW(TAG, "==================================================");
            ESP_LOGW(TAG, "  NEW CREDENTIALS GENERATED");
            ESP_LOGW(TAG, "  Admin Token:     %s", dc->admin_token);
            ESP_LOGW(TAG, "  Installer Token: %s", dc->installer_token);
            ESP_LOGW(TAG, "  Operator Token:  %s", dc->operator_token);
            ESP_LOGW(TAG, "  STORE THESE SECURELY. THEY WILL NOT BE SHOWN AGAIN.");
            ESP_LOGW(TAG, "==================================================");

            esp_err_t cfg_err = save_config(*dc);
            if (cfg_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to persist provisioned credentials: %s", esp_err_to_name(cfg_err));
            }
        }
    }
}

} // namespace lg
