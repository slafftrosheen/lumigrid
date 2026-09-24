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
#include "lg_board_pins.hpp"
#include "ChannelHardware.hpp"

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

uint32_t calc_device_crc(const DeviceConfigV2& dc) {
    return esp_rom_crc32_le(0, (const uint8_t*)&dc, sizeof(DeviceConfigV2) - sizeof(uint32_t));
}

struct SequenceV1 {
    SequencerTrack tracks[8] = {};
    uint16_t bpm = 120;
    bool enabled = false;
    uint32_t checksum = 0;
};

uint32_t calc_seq_crc(const Sequence& s) {
    return esp_rom_crc32_le(0, (const uint8_t*)&s, sizeof(Sequence) - sizeof(uint32_t));
}

uint32_t calc_seq_v1_crc(const SequenceV1& s) {
    return esp_rom_crc32_le(0, (const uint8_t*)&s, sizeof(SequenceV1) - sizeof(uint32_t));
}

uint32_t calc_calendar_crc(const CalendarSchedule& c) {
    return esp_rom_crc32_le(0, (const uint8_t*)&c, sizeof(CalendarSchedule) - sizeof(uint32_t));
}

uint32_t calc_motion_crc(const SceneMotionConfig& m) {
    return esp_rom_crc32_le(0, (const uint8_t*)&m, sizeof(SceneMotionConfig) - sizeof(uint32_t));
}

uint32_t calc_cue_list_crc(const CueList& cl) {
    return esp_rom_crc32_le(0, (const uint8_t*)&cl, sizeof(CueList) - sizeof(uint32_t));
}

void clear_scene_motion(SceneMotionConfig& m) {
    auto* p = reinterpret_cast<uint8_t*>(&m);
    for (size_t i = 0; i < sizeof(SceneMotionConfig); ++i) {
        p[i] = 0;
    }
    m.schema_version = 1;
}

void clear_cue_list(CueList& cl) {
    auto* p = reinterpret_cast<uint8_t*>(&cl);
    for (size_t i = 0; i < sizeof(CueList); ++i) {
        p[i] = 0;
    }
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

    SceneConfig to_save = scene;
    to_save.checksum = calc_scene_crc(to_save);

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, key, &to_save, sizeof(SceneConfig)),
        TAG, "save scene primary"
    );
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), TAG, "commit scene primary");

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
    esp_err_t bak_err = nvs_set_blob(handle_, bak_key, &to_save, sizeof(SceneConfig));
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

    // Legacy scenes were zone-based. Resolve topology lazily only when an old
    // scene blob is actually encountered; normal v2 scene loads stay NVS-light.
    DeviceConfig topology_cfg;
    bool topology_checked = false;
    bool have_topology = false;
    bool migrated_legacy_scene = false;
    auto legacy_zone_for_channel = [&](uint8_t channel) -> uint8_t {
        if (!topology_checked) {
            have_topology = (load_config(topology_cfg) == ESP_OK);
            topology_checked = true;
        }
        if (have_topology && channel < 16) {
            const uint16_t bit = static_cast<uint16_t>(1U << channel);
            for (uint8_t z = 0; z < 8; ++z) {
                if ((topology_cfg.groups[z].channel_mask & bit) != 0) {
                    return z;
                }
            }
        }
        // Legacy v1 migration assigned ungrouped fixtures to zone 0.
        return 0;
    };

    auto load_from_key = [&](const char* k, SceneConfig& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len == sizeof(SceneConfig)) {
                len = sizeof(SceneConfig);
                err = nvs_get_blob(handle_, k, &out, &len);
                if (err == ESP_OK) {
                    if (out.checksum != calc_scene_crc(out)) {
                        ESP_LOGW(TAG, "Scene %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    return ESP_OK;
                }
            } else if (len == sizeof(SceneConfigV1)) {
                auto v1 = std::make_unique<SceneConfigV1>();
                err = nvs_get_blob(handle_, k, v1.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v1.get(), sizeof(SceneConfigV1) - sizeof(uint32_t));
                    if (v1->checksum != expected_crc) {
                        ESP_LOGW(TAG, "SceneConfigV1 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    out.reset();
                    safe_strncpy(out.name, v1->name, sizeof(out.name));
                    out.global_brightness = v1->global_brightness;
                    out.transition_ms = v1->transition_ms;
                    out.schema_version = 2;
                    for (uint8_t ch = 0; ch < 16; ++ch) {
                        for (uint8_t l = 0; l < 3; ++l) {
                            out.channels[ch].layers[l] = v1->zones[legacy_zone_for_channel(ch)].layers[l];
                        }
                    }
                    out.checksum = calc_scene_crc(out);
                    migrated_legacy_scene = true;
                    ESP_LOGI(TAG, "Migrated SceneConfigV1 (8-zone) %s to v2 (16-channel)", k);
                    return ESP_OK;
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
                
                OldSceneConfig old{};
                len = sizeof(OldSceneConfig);
                err = nvs_get_blob(handle_, k, &old, &len);
                if (err == ESP_OK) {
                    // Check legacy CRC (using the same offset logic)
                    uint32_t expected_crc = ~0U;
                    expected_crc = esp_rom_crc32_le(expected_crc, reinterpret_cast<const uint8_t*>(&old), offsetof(OldSceneConfig, checksum));
                    if (old.checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy Scene %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    
                    out.reset();
                    std::memcpy(out.name, old.name, sizeof(out.name) - 1);
                    out.name[sizeof(out.name) - 1] = '\0';
                    out.global_brightness = old.global_brightness;
                    out.transition_ms = old.transition_ms;
                    out.schema_version = 2;
                    
                    for (int ch = 0; ch < 16; ++ch) {
                        const uint8_t i = legacy_zone_for_channel(static_cast<uint8_t>(ch));
                        out.channels[ch].layers[0].effect = old.zones[i].effect;
                        out.channels[ch].layers[0].primary_color = old.zones[i].primary_color;
                        out.channels[ch].layers[0].secondary_color = old.zones[i].secondary_color;
                        out.channels[ch].layers[0].speed_ms = old.zones[i].speed_ms;
                        out.channels[ch].layers[0].fx_param = old.zones[i].fx_param;
                        out.channels[ch].layers[0].palette_id = old.zones[i].palette_id;
                        out.channels[ch].layers[0].opacity = 255;
                        out.channels[ch].layers[0].blend_mode = BlendMode::Normal;
                        out.channels[ch].layers[0].enabled = true;
                    }
                    
                    out.checksum = calc_scene_crc(out);
                    migrated_legacy_scene = true;
                    ESP_LOGI(TAG, "Migrated legacy scene %s", k);
                    return ESP_OK;
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

    if (err == ESP_OK && migrated_legacy_scene) {
        // Persist the channel-based representation immediately so later scene
        // playback never has to re-run legacy topology resolution.
        const esp_err_t persist_err = save_scene(index, scene);
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist migrated scene %u: %s", index, esp_err_to_name(persist_err));
        }
    }
    
    return err;
}

esp_err_t ConfigStore::delete_scene(uint8_t index) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
    (void)nvs_erase_key(handle_, bak_key);
    (void)delete_scene_motion(index);

    esp_err_t err = nvs_erase_key(handle_, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "delete scene");
    return nvs_commit(handle_);
}

uint8_t ConfigStore::scene_count() {
    uint8_t count = 0;
    SceneConfig tmp;
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        if (load_scene(i, tmp) == ESP_OK) ++count;
    }
    return count;
}

bool ConfigStore::scene_exists(uint8_t index) const {
    if (index >= kMaxPresets) return false;
    SceneConfig sc;
    return const_cast<ConfigStore*>(this)->load_scene(index, sc) == ESP_OK;
}

uint32_t ConfigStore::compute_playback_hash() const {
    uint32_t hash = 0;
    const uint32_t show_schema = 2;
    hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&show_schema), sizeof(show_schema));

    // Ordered scene checksums (slots 0..31)
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneConfig scene;
        uint32_t crc = 0;
        if (const_cast<ConfigStore*>(this)->load_scene(i, scene) == ESP_OK) {
            crc = scene.checksum;
        }
        hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
    }

    // Ordered scene motion checksums (slots 0..31)
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneMotionConfig motion;
        uint32_t crc = 0;
        if (const_cast<ConfigStore*>(this)->load_scene_motion(i, motion) == ESP_OK) {
            crc = motion.checksum;
        }
        hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
    }

    // Sequence (Timeline) checksum
    {
        Sequence seq;
        uint32_t crc = 0;
        if (const_cast<ConfigStore*>(this)->load_sequence(seq) == ESP_OK) {
            crc = seq.checksum;
        }
        hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
    }

    // Ordered cue-list checksums (0..kMaxCueLists-1)
    for (uint8_t i = 0; i < kMaxCueLists; ++i) {
        CueList cl;
        uint32_t crc = 0;
        if (const_cast<ConfigStore*>(this)->load_cue_list(i, cl) == ESP_OK) {
            crc = cl.checksum;
        }
        hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
    }

    return hash;
}

uint32_t ConfigStore::compute_schedule_hash() const {
    uint32_t hash = 0;
    const uint32_t show_schema = 2;
    hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&show_schema), sizeof(show_schema));

    CalendarSchedule sched;
    uint32_t crc = 0;
    if (const_cast<ConfigStore*>(this)->load_calendar_schedule(sched) == ESP_OK) {
        crc = sched.checksum;
    }
    hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
    return hash;
}

uint32_t ConfigStore::compute_full_show_hash() const {
    const uint32_t pb = compute_playback_hash();
    const uint32_t sc = compute_schedule_hash();
    uint32_t hash = 0;
    hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&pb), sizeof(pb));
    hash = esp_rom_crc32_le(hash, reinterpret_cast<const uint8_t*>(&sc), sizeof(sc));
    return hash;
}

uint32_t ConfigStore::compute_show_hash() const {
    // Phase 1: showHash is a compatibility alias for playbackHash
    return compute_playback_hash();
}

// ── Scene Motion ────────────────────────────────────────────────────

esp_err_t ConfigStore::save_scene_motion(uint8_t index, const SceneMotionConfig& motion) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    SceneMotionConfig to_save = motion;
    to_save.schema_version = 1;
    to_save.checksum = calc_motion_crc(to_save);

    char key[16];
    make_key(key, sizeof(key), "smot", index);

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, key, &to_save, sizeof(SceneMotionConfig)),
        TAG, "save scene motion primary"
    );
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), TAG, "commit scene motion primary");

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "smot_b_%u", index);
    esp_err_t bak_err = nvs_set_blob(handle_, bak_key, &to_save, sizeof(SceneMotionConfig));
    if (bak_err == ESP_OK) {
        (void)nvs_commit(handle_);
    }

    return ESP_OK;
}

esp_err_t ConfigStore::load_scene_motion(uint8_t index, SceneMotionConfig& motion) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;
    clear_scene_motion(motion);

    char key[16];
    make_key(key, sizeof(key), "smot", index);

    size_t len = 0;
    esp_err_t err = nvs_get_blob(handle_, key, nullptr, &len);
    if (err == ESP_OK && len == sizeof(SceneMotionConfig)) {
        err = nvs_get_blob(handle_, key, &motion, &len);
        if (err == ESP_OK) {
            if (motion.checksum != calc_motion_crc(motion)) {
                ESP_LOGW(TAG, "Scene motion %s CRC mismatch, trying backup", key);
                char bak_key[16];
                snprintf(bak_key, sizeof(bak_key), "smot_b_%u", index);
                len = sizeof(SceneMotionConfig);
                err = nvs_get_blob(handle_, bak_key, &motion, &len);
                if (err == ESP_OK && len == sizeof(SceneMotionConfig) && motion.checksum == calc_motion_crc(motion)) {
                    (void)nvs_set_blob(handle_, key, &motion, sizeof(SceneMotionConfig));
                    (void)nvs_commit(handle_);
                    return ESP_OK;
                }
                clear_scene_motion(motion);
                return ESP_ERR_NVS_INVALID_STATE;
            }
            return ESP_OK;
        }
    }

    // Check backup
    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "smot_b_%u", index);
    len = 0;
    if (nvs_get_blob(handle_, bak_key, nullptr, &len) == ESP_OK && len == sizeof(SceneMotionConfig)) {
        if (nvs_get_blob(handle_, bak_key, &motion, &len) == ESP_OK && motion.checksum == calc_motion_crc(motion)) {
            (void)nvs_set_blob(handle_, key, &motion, sizeof(SceneMotionConfig));
            (void)nvs_commit(handle_);
            return ESP_OK;
        }
    }

    clear_scene_motion(motion);
    return (err == ESP_OK) ? ESP_ERR_NOT_FOUND : err;
}

esp_err_t ConfigStore::delete_scene_motion(uint8_t index) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[16];
    make_key(key, sizeof(key), "smot", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "smot_b_%u", index);
    (void)nvs_erase_key(handle_, bak_key);

    esp_err_t err = nvs_erase_key(handle_, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "delete scene motion");
    return nvs_commit(handle_);
}

// ── Cue Lists ───────────────────────────────────────────────────────

esp_err_t ConfigStore::save_cue_list(uint8_t index, const CueList& list) {
    if (index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;

    CueList to_save = list;
    to_save.checksum = calc_cue_list_crc(to_save);

    char key[16];
    make_key(key, sizeof(key), "cuel", index);

    ESP_RETURN_ON_ERROR(
        nvs_set_blob(handle_, key, &to_save, sizeof(CueList)),
        TAG, "save cue list primary"
    );
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), TAG, "commit cue list primary");

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "cuel_b_%u", index);
    esp_err_t bak_err = nvs_set_blob(handle_, bak_key, &to_save, sizeof(CueList));
    if (bak_err == ESP_OK) {
        (void)nvs_commit(handle_);
    }

    return ESP_OK;
}

esp_err_t ConfigStore::load_cue_list(uint8_t index, CueList& list) {
    if (index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;
    clear_cue_list(list);

    char key[16];
    make_key(key, sizeof(key), "cuel", index);

    size_t len = 0;
    esp_err_t err = nvs_get_blob(handle_, key, nullptr, &len);
    if (err == ESP_OK && len == sizeof(CueList)) {
        err = nvs_get_blob(handle_, key, &list, &len);
        if (err == ESP_OK) {
            if (list.checksum != calc_cue_list_crc(list)) {
                ESP_LOGW(TAG, "Cue list %s CRC mismatch, trying backup", key);
                char bak_key[16];
                snprintf(bak_key, sizeof(bak_key), "cuel_b_%u", index);
                len = sizeof(CueList);
                err = nvs_get_blob(handle_, bak_key, &list, &len);
                if (err == ESP_OK && len == sizeof(CueList) && list.checksum == calc_cue_list_crc(list)) {
                    (void)nvs_set_blob(handle_, key, &list, sizeof(CueList));
                    (void)nvs_commit(handle_);
                    return ESP_OK;
                }
                clear_cue_list(list);
                return ESP_ERR_NVS_INVALID_STATE;
            }
            return ESP_OK;
        }
    }

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "cuel_b_%u", index);
    len = 0;
    if (nvs_get_blob(handle_, bak_key, nullptr, &len) == ESP_OK && len == sizeof(CueList)) {
        if (nvs_get_blob(handle_, bak_key, &list, &len) == ESP_OK && list.checksum == calc_cue_list_crc(list)) {
            (void)nvs_set_blob(handle_, key, &list, sizeof(CueList));
            (void)nvs_commit(handle_);
            return ESP_OK;
        }
    }

    clear_cue_list(list);
    return (err == ESP_OK) ? ESP_ERR_NOT_FOUND : err;
}

esp_err_t ConfigStore::delete_cue_list(uint8_t index) {
    if (index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;

    char key[16];
    make_key(key, sizeof(key), "cuel", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "cuel_b_%u", index);
    (void)nvs_erase_key(handle_, bak_key);

    esp_err_t err = nvs_erase_key(handle_, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    ESP_RETURN_ON_ERROR(err, TAG, "delete cue list");
    return nvs_commit(handle_);
}

uint8_t ConfigStore::cue_list_count() {
    uint8_t count = 0;
    CueList tmp;
    for (uint8_t i = 0; i < kMaxCueLists; ++i) {
        if (load_cue_list(i, tmp) == ESP_OK && tmp.count > 0) ++count;
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
            required_channels = 0;
            return false;
        }
        uint32_t ch_per_pixel = (fixture.profile == FixtureProfile::SK6812 || fixture.profile == FixtureProfile::PWM_RGBW) ? 4 : 3;
        required_channels = static_cast<uint32_t>(fixture.num_pixels) * ch_per_pixel;
    } else {
        required_channels = 0;
        return false;
    }

    uint32_t start = static_cast<uint32_t>(fixture.dmx_start_channel) - 1;
    uint32_t end_exclusive = start + required_channels;

    if (fixture.dmx_start_channel < 1 || end_exclusive > 512) {
        return false;
    }

    return true;
}

bool ConfigStore::calculate_dmx_span(const ChannelConfig& channel, uint8_t channel_index, uint32_t& required_channels) {
    if (channel.dmx_start_channel == 0) {
        required_channels = 0;
        return true;
    }

    const auto hw = channel_hardware(channel_index);
    if (hw.kind == ChannelKind::Pwm) {
        // PWM channels on the PCB are single dimmer outputs: 1 DMX slot
        required_channels = 1;
    } else {
        if (channel.dmx_mode == 0) {
            // Wash mode
            required_channels = (channel.addressable_type == AddressableType::SK6812) ? 4 : 3;
        } else if (channel.dmx_mode == 1) {
            // Direct pixel map mode
            if (channel.pixel_count == 0) {
                required_channels = 0;
                return false;
            }
            uint32_t ch_per_pixel = (channel.addressable_type == AddressableType::SK6812) ? 4 : 3;
            required_channels = static_cast<uint32_t>(channel.pixel_count) * ch_per_pixel;
        } else {
            required_channels = 0;
            return false;
        }
    }

    uint32_t start = static_cast<uint32_t>(channel.dmx_start_channel) - 1;
    uint32_t end_exclusive = start + required_channels;

    if (channel.dmx_start_channel < 1 || end_exclusive > 512) {
        return false;
    }

    return true;
}

bool ConfigStore::is_fixture_configured(const FixtureConfig& fix, const DeviceConfig& cfg) {
    (void)cfg;
    const bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
    const bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                         fix.profile == FixtureProfile::PWM_RGB ||
                         fix.profile == FixtureProfile::PWM_RGBW);
    if (!is_addressable && !is_pwm) {
        return false;
    }
    if (is_addressable) {
        return fix.num_pixels > 0;
    }
    return fix.dmx_start_channel > 0;
}

bool ConfigStore::has_configured_fixtures(const DeviceConfig& config) {
    for (uint8_t i = 0; i < kChannelCount; ++i) {
        if (config.channels[i].enabled) {
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

    // 3. Channels validation (LG-023 16-Channel Hardware Model)
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const auto& c = config.channels[ch];
        if (!c.enabled) {
            continue;
        }

        const auto hw = channel_hardware(ch);
        if (hw.kind == ChannelKind::Addressable) {
            if (c.pixel_count > MAX_PIXELS) {
                add_error("channel[%u].pixel_count (%u) exceeds MAX_PIXELS (%u)",
                          (unsigned)ch, (unsigned)c.pixel_count, (unsigned)MAX_PIXELS);
            }
        }

        if (c.dmx_start_channel > 0) {
            uint32_t req_channels = 0;
            if (!calculate_dmx_span(c, ch, req_channels)) {
                add_error("channel[%u] DMX patch invalid (start=%u, mode=%u)",
                          (unsigned)ch, (unsigned)c.dmx_start_channel, (unsigned)c.dmx_mode);
            }
            if (c.dmx_universe > 63999) {
                add_error("channel[%u].dmx_universe (%u) exceeds valid range 63999",
                          (unsigned)ch, (unsigned)c.dmx_universe);
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

bool ConfigStore::migrate_v1_to_v2(const DeviceConfigV1& v1, DeviceConfigV2& v2, uint32_t* out_warning_count) {
    v2 = DeviceConfigV2{};
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

        // Count matching zones from legacy v1 fixture_mask
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
            if (out_warning_count) (*out_warning_count)++;
        } else if (match_count == 1) {
            v2.fixtures[f].zone_id = first_zone;
        } else {
            v2.fixtures[f].zone_id = first_zone;
            if (out_warning_count) (*out_warning_count)++;
        }

        bool is_addressable = (v2.fixtures[f].profile == FixtureProfile::WS2812 || v2.fixtures[f].profile == FixtureProfile::SK6812);
        if (is_addressable && v2.fixtures[f].num_pixels > MAX_PIXELS) {
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

bool ConfigStore::migrate_v2_to_v3(const DeviceConfigV2& v2, DeviceConfig& v3, uint32_t* out_warning_count) {
    v3.reset();
    v3.schema_version = 3;
    safe_strncpy(v3.device_name, v2.device_name, sizeof(v3.device_name));
    v3.active_preset_index = v2.active_preset_index;
    v3.fallback_preset_index = v2.fallback_preset_index;
    v3.watchdog_timeout_s = v2.watchdog_timeout_s;
    v3.max_power_mw = v2.max_power_mw;
    v3.gamma_x10 = v2.gamma_x10;
    v3.soft_start_enabled = v2.soft_start_enabled;
    v3.soft_start_ms = v2.soft_start_ms;
    v3.mqtt_enabled = v2.mqtt_enabled;
    safe_strncpy(v3.mqtt_broker, v2.mqtt_broker, sizeof(v3.mqtt_broker));
    v3.mqtt_port = v2.mqtt_port;
    safe_strncpy(v3.timezone, v2.timezone, sizeof(v3.timezone));
    v3.sync_enabled = v2.sync_enabled;
    v3.sync_role = v2.sync_role;
    v3.sync_token = v2.sync_token;

    safe_strncpy(v3.admin_token, v2.admin_token, sizeof(v3.admin_token));
    safe_strncpy(v3.installer_token, v2.installer_token, sizeof(v3.installer_token));
    safe_strncpy(v3.operator_token, v2.operator_token, sizeof(v3.operator_token));

    safe_strncpy(v3.serial_number, v2.serial_number, sizeof(v3.serial_number));
    safe_strncpy(v3.hardware_revision, v2.hardware_revision, sizeof(v3.hardware_revision));
    safe_strncpy(v3.site_label, v2.site_label, sizeof(v3.site_label));
    safe_strncpy(v3.group_label, v2.group_label, sizeof(v3.group_label));

    // Initialize all 16 physical channels as present-but-unconfigured.
    // Port identity (A1..A8/P1..P8) comes from ChannelHardware, not user labels.
    for (uint8_t ch = 0; ch < 16; ++ch) {
        v3.channels[ch] = ChannelConfig{};
    }

    // Migrate groups from legacy zones
    for (uint8_t z = 0; z < 8; ++z) {
        safe_strncpy(v3.groups[z].name, v2.zones[z].name, sizeof(v3.groups[z].name));
        v3.groups[z].role = v2.zones[z].role;
        v3.groups[z].channel_mask = 0;
        v3.groups[z].enabled = (v3.groups[z].name[0] != '\0');
    }

    // Migrate fixtures to channels
    for (uint8_t f = 0; f < 16; ++f) {
        const auto& fix = v2.fixtures[f];
        bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
        bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                       fix.profile == FixtureProfile::PWM_RGB ||
                       fix.profile == FixtureProfile::PWM_RGBW);
        if (!is_addressable && !is_pwm) continue;

        if (is_addressable) {
            uint8_t p_idx = fix.physical_output_index[0];
            uint8_t hw_id = (p_idx < 16) ? v2.physical_outputs[p_idx].hardware_id : (f % 8);
            if (hw_id >= 8) hw_id = f % 8;
            uint8_t ch = hw_id; // Channels 0..7 are A1..A8

            v3.channels[ch].enabled = (fix.num_pixels > 0);
            v3.channels[ch].addressable_type = (fix.profile == FixtureProfile::SK6812) ? AddressableType::SK6812 : AddressableType::WS2812;
            v3.channels[ch].pixel_order = fix.pixel_order;
            v3.channels[ch].pixel_count = fix.num_pixels;
            v3.channels[ch].max_brightness = fix.max_brightness;
            v3.channels[ch].gamma_correction = fix.gamma_correction;
            v3.channels[ch].max_milliwatts = fix.max_milliwatts;
            v3.channels[ch].dmx_universe = fix.dmx_universe;
            v3.channels[ch].dmx_start_channel = fix.dmx_start_channel;
            v3.channels[ch].dmx_mode = fix.dmx_mode;

            if (fix.zone_id < 8) {
                v3.groups[fix.zone_id].channel_mask |= (1U << ch);
            }
        } else if (is_pwm) {
            const uint8_t channels_used =
                (fix.profile == FixtureProfile::PWM_Single) ? 1 :
                (fix.profile == FixtureProfile::PWM_RGB ? 3 : 4);

            for (uint8_t component = 0; component < channels_used; ++component) {
                const uint8_t p_idx = fix.physical_output_index[component];
                if (p_idx >= 16) {
                    if (out_warning_count) (*out_warning_count)++;
                    continue;
                }

                const auto& phys = v2.physical_outputs[p_idx];
                if (!phys.enabled || phys.type != OutputType::PWM || phys.hardware_id >= 8) {
                    if (out_warning_count) (*out_warning_count)++;
                    continue;
                }

                const uint8_t ch = static_cast<uint8_t>(8 + phys.hardware_id);
                auto& dst = v3.channels[ch];
                dst.enabled = true;
                dst.max_brightness = fix.max_brightness;
                dst.gamma_correction = fix.gamma_correction;
                dst.max_milliwatts = fix.max_milliwatts == 0
                    ? 0
                    : static_cast<uint16_t>((fix.max_milliwatts + channels_used - 1) / channels_used);
                dst.dmx_universe = fix.dmx_universe;
                dst.dmx_start_channel = fix.dmx_start_channel == 0
                    ? 0
                    : static_cast<uint16_t>(fix.dmx_start_channel + component);

                // Preserve old compound PWM colour semantics without exposing
                // compound fixtures in the new UI. 0 is normal mono; 2..5 are
                // migration-only R/G/B/W component selectors.
                dst.dmx_mode = (fix.profile == FixtureProfile::PWM_Single)
                    ? 0
                    : static_cast<uint8_t>(2 + component);

                if (fix.zone_id < 8) {
                    v3.groups[fix.zone_id].channel_mask |= static_cast<uint16_t>(1U << ch);
                }
            }

            if (fix.profile == FixtureProfile::PWM_RGB || fix.profile == FixtureProfile::PWM_RGBW) {
                ESP_LOGI(TAG, "Legacy compound PWM fixture %u decomposed into %u physical P channels",
                         f, channels_used);
            }
        }
    }

    v3.protocol = v2.protocol;
    v3.checksum = calc_device_crc(v3);
    return true;
}

esp_err_t ConfigStore::load_config(DeviceConfig& config) {
    bool needs_persist = false;

    auto load_from_key = [&](const char* k, DeviceConfig& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len > 0) {
            if (len == sizeof(DeviceConfig)) {
                len = sizeof(DeviceConfig);
                err = nvs_get_blob(handle_, k, &out, &len);
                if (err == ESP_OK) {
                    if (out.checksum != calc_device_crc(out)) {
                        ESP_LOGW(TAG, "DeviceConfig %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    if (out.schema_version < 3) {
                        ESP_LOGI(TAG, "Migrating DeviceConfig %s from in-memory schema v%u to v3", k, out.schema_version);
                        out.schema_version = 3;
                        out.checksum = calc_device_crc(out);
                        needs_persist = true;
                    }
                    return ESP_OK;
                }
            } else if (len == sizeof(DeviceConfigV2)) {
                auto v2 = std::make_unique<DeviceConfigV2>();
                err = nvs_get_blob(handle_, k, v2.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v2.get(), sizeof(DeviceConfigV2) - sizeof(uint32_t));
                    if (v2->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy DeviceConfigV2 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    ESP_LOGI(TAG, "Migrating legacy v2 blob %s to v3", k);
                    migrate_v2_to_v3(*v2, out);
                    needs_persist = true;
                    return ESP_OK;
                }
            } else if (len == sizeof(DeviceConfigV1)) {
                auto v1 = std::make_unique<DeviceConfigV1>();
                err = nvs_get_blob(handle_, k, v1.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v1.get(), sizeof(DeviceConfigV1) - sizeof(uint32_t));
                    if (v1->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy DeviceConfigV1 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    ESP_LOGI(TAG, "Migrating legacy v1 blob %s through v2 to v3", k);
                    DeviceConfigV2 v2;
                    migrate_v1_to_v2(*v1, v2);
                    migrate_v2_to_v3(v2, out);
                    needs_persist = true;
                    return ESP_OK;
                }
            } else {
                ESP_LOGW(TAG, "DeviceConfig size mismatch (len=%u, expected=%u)",
                         (unsigned)len, (unsigned)sizeof(DeviceConfig));
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

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_STATE || err == ESP_ERR_NVS_INVALID_LENGTH) {
        config.reset();
        ESP_LOGI(TAG, "Default credentials active (Admin role configured)");
        populate_default_topology(config);
        config.schema_version = 3;
        config.checksum = calc_device_crc(config);
        save_config(config);
        return ESP_OK;
    } else if (err == ESP_OK) {
        if (needs_persist) {
            ESP_LOGI(TAG, "Persisting migrated/updated DeviceConfig to NVS");
            esp_err_t s_err = save_config(config);
            if (s_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to persist migrated DeviceConfig to NVS: %s", esp_err_to_name(s_err));
                return s_err;
            }

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

void ConfigStore::populate_default_topology(DeviceConfig& config) {
    // Hardware identity is immutable and supplied by ChannelHardware. Config
    // defaults describe zero connected loads: every connector is visible in the
    // API/UI but remains disabled until the operator configures it.
    for (uint8_t i = 0; i < 16; ++i) {
        config.channels[i] = ChannelConfig{};
    }

    // Useful selection groups may exist on a fresh node without implying that
    // any physical output is configured or enabled.
    config.groups[0] = ChannelGroup{};
    safe_strncpy(config.groups[0].name, "All Addressable", sizeof(config.groups[0].name));
    config.groups[0].channel_mask = 0x00FF;
    config.groups[0].enabled = true;

    config.groups[1] = ChannelGroup{};
    safe_strncpy(config.groups[1].name, "All PWM", sizeof(config.groups[1].name));
    config.groups[1].channel_mask = 0xFF00;
    config.groups[1].enabled = true;
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
    bool migrated = false;

    auto load_from_key = [&](const char* k, Sequence& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err != ESP_OK || len == 0) return err;

        if (len == sizeof(Sequence)) {
            len = sizeof(Sequence);
            err = nvs_get_blob(handle_, k, &out, &len);
            if (err != ESP_OK) return err;
            if (out.checksum != calc_seq_crc(out)) {
                ESP_LOGW(TAG, "Sequence %s CRC mismatch", k);
                return ESP_ERR_NVS_INVALID_STATE;
            }
            return ESP_OK;
        }

        if (len == sizeof(SequenceV1)) {
            SequenceV1 old{};
            size_t old_len = sizeof(old);
            err = nvs_get_blob(handle_, k, &old, &old_len);
            if (err != ESP_OK) return err;
            if (old.checksum != calc_seq_v1_crc(old)) {
                ESP_LOGW(TAG, "Legacy 8-track sequence %s CRC mismatch", k);
                return ESP_ERR_NVS_INVALID_STATE;
            }

            out = Sequence{};
            for (uint8_t i = 0; i < 8; ++i) {
                out.tracks[i] = old.tracks[i];
            }
            for (uint8_t i = 8; i < 16; ++i) {
                out.tracks[i] = SequencerTrack{};
                out.tracks[i].channel_id = i;
                out.tracks[i].enabled = false;
            }
            out.bpm = old.bpm;
            out.enabled = old.enabled;
            out.checksum = calc_seq_crc(out);
            migrated = true;
            ESP_LOGI(TAG, "Migrated sequence %s from 8 tracks to 16 physical-channel tracks", k);
            return ESP_OK;
        }

        return ESP_ERR_NVS_INVALID_LENGTH;
    };

    esp_err_t err = load_from_key("seq_main", seq);
    if (err == ESP_ERR_NVS_INVALID_STATE || err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGW(TAG, "Restoring Sequence from backup slot");
        err = load_from_key("seq_main_bak", seq);
    }

    if (err == ESP_OK && migrated) {
        const esp_err_t persist_err = save_sequence(seq);
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist migrated 16-track sequence: %s", esp_err_to_name(persist_err));
            return persist_err;
        }
    }

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_STATE || err == ESP_ERR_NVS_INVALID_LENGTH) {
        seq = Sequence{};
        for (uint8_t i = 0; i < 16; ++i) {
            seq.tracks[i].channel_id = i;
        }
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
                len = sizeof(CalendarSchedule);
                err = nvs_get_blob(handle_, k, &out, &len);
                if (err == ESP_OK) {
                    if (out.checksum != calc_calendar_crc(out)) {
                        ESP_LOGW(TAG, "Calendar %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    return ESP_OK;
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

// ── Network settings (LG-027) ───────────────────────────────────────

esp_err_t ConfigStore::save_network_settings(const NetworkSettings& settings) {
    if (!handle_) return ESP_ERR_INVALID_STATE;

    NetworkSettings to_save = settings;
    to_save.schema_version = 1;
    to_save.checksum = calc_network_settings_crc(to_save);

    // 1. Write and commit primary slot ("network")
    esp_err_t err = nvs_set_blob(handle_, "network", &to_save, sizeof(NetworkSettings));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write primary network settings: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit primary network settings: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Read-back verification of primary
    NetworkSettings verify_buf{};
    size_t verify_len = sizeof(NetworkSettings);
    err = nvs_get_blob(handle_, "network", &verify_buf, &verify_len);
    if (err != ESP_OK || verify_len != sizeof(NetworkSettings) ||
        verify_buf.checksum != to_save.checksum ||
        calc_network_settings_crc(verify_buf) != to_save.checksum) {
        ESP_LOGE(TAG, "Primary network settings read-back verification failed! Preserving backup intact.");
        return ESP_ERR_NVS_INVALID_STATE;
    }

    // 3. Primary confirmed valid -> update backup slot ("network_b")
    err = nvs_set_blob(handle_, "network_b", &to_save, sizeof(NetworkSettings));
    if (err == ESP_OK) {
        (void)nvs_commit(handle_);
    } else {
        ESP_LOGW(TAG, "Primary network settings committed, but writing backup slot failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

esp_err_t ConfigStore::load_network_settings(NetworkSettings& settings) {
    if (!handle_) return ESP_ERR_INVALID_STATE;

    auto load_from_key = [&](const char* k, NetworkSettings& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len == sizeof(NetworkSettings)) {
            err = nvs_get_blob(handle_, k, &out, &len);
            if (err == ESP_OK) {
                if (out.checksum != calc_network_settings_crc(out)) {
                    ESP_LOGW(TAG, "NetworkSettings %s CRC mismatch", k);
                    return ESP_ERR_NVS_INVALID_STATE;
                }
                return ESP_OK;
            }
        }
        return (err == ESP_OK) ? ESP_ERR_NVS_INVALID_LENGTH : err;
    };

    // Try primary "network"
    esp_err_t err = load_from_key("network", settings);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    // Primary missing or corrupted -> attempt recovery from backup "network_b"
    ESP_LOGW(TAG, "Restoring NetworkSettings from backup slot 'network_b'...");
    err = load_from_key("network_b", settings);
    if (err == ESP_OK) {
        // Restore primary from valid backup
        (void)nvs_set_blob(handle_, "network", &settings, sizeof(NetworkSettings));
        (void)nvs_commit(handle_);
        return ESP_OK;
    }

    // Neither slot valid -> populate default NetworkSettings
    settings = NetworkSettings{};
    settings.schema_version = 1;
    settings.mode = NetworkMode::ApOnly;
    settings.sta_ssid[0] = '\0';
    settings.sta_password[0] = '\0';
    settings.ap_enabled = true;
    settings.ap_name[0] = '\0';
    settings.ap_channel = 1;
    settings.checksum = calc_network_settings_crc(settings);

    return ESP_ERR_NOT_FOUND;
}

// ── Defaults ────────────────────────────────────────────────────────

void ConfigStore::ensure_defaults() {
    auto test = std::make_unique<SceneConfig>();
    if (load_scene(0, *test) == ESP_OK) return; // Already has scenes

    ESP_LOGI(TAG, "No scenes found, creating defaults...");

    // ── Product-Grade Default Scenes ──
    auto s_open = std::make_unique<SceneConfig>();
    s_open->reset();
    safe_strncpy(s_open->name, "Store Open", sizeof(s_open->name));
    s_open->global_brightness = 180;
    s_open->schema_version = 2;
    for (uint8_t i = 0; i < 16; ++i) {
        s_open->channels[i].layers[0].effect = static_cast<uint8_t>(SignageEffect::Solid);
        s_open->channels[i].layers[0].primary_color = {255, 200, 150};
        s_open->channels[i].layers[0].enabled = true;
        s_open->channels[i].layers[0].opacity = 255;
    }
    esp_err_t err = save_scene(0, *s_open);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save default scene 0: %s", esp_err_to_name(err));
    }

    auto s_night = std::make_unique<SceneConfig>();
    s_night->reset();
    safe_strncpy(s_night->name, "Night Mode", sizeof(s_night->name));
    s_night->global_brightness = 40;
    s_night->schema_version = 2;
    for (uint8_t i = 0; i < 16; ++i) {
        s_night->channels[i].layers[0].effect = static_cast<uint8_t>(SignageEffect::Breathe);
        s_night->channels[i].layers[0].primary_color = {20, 40, 120};
        s_night->channels[i].layers[0].enabled = true;
        s_night->channels[i].layers[0].opacity = 255;
    }
    err = save_scene(1, *s_night);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save default scene 1: %s", esp_err_to_name(err));
    }

}

} // namespace lg
