// components/config/ConfigStore.cpp
#include "ConfigStore.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <initializer_list>
#include <memory>
#include <new>
#include "esp_rom_crc.h"
#include "lg_board_pins.hpp"
#include "ChannelHardware.hpp"
#include "NvsRuntime.hpp"

static const char* TAG = "config";

namespace lg {
namespace {

void safe_strncpy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0) return;
    // Source may be a short literal or a fixed-width legacy NVS field. Stop at
    // its terminator and zero-pad, without reading past a short source object.
    std::strncpy(dst, src, dst_size - 1);
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

uint32_t calc_device_crc(const DeviceConfigV3& dc) {
    return esp_rom_crc32_le(0, (const uint8_t*)&dc, sizeof(DeviceConfigV3) - sizeof(uint32_t));
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

template<size_t N>
bool terminated(const char (&text)[N]) {
    return std::memchr(text, '\0', N) != nullptr;
}

bool recoverable_blob_error(esp_err_t error) {
    return error == ESP_ERR_NVS_NOT_FOUND || error == ESP_ERR_NVS_INVALID_LENGTH ||
           error == ESP_ERR_NVS_INVALID_STATE || error == ESP_ERR_INVALID_VERSION;
}

esp_err_t fallback_result(esp_err_t primary, esp_err_t backup) {
    // An absent backup must not hide a corrupt primary or a real I/O error.
    return backup == ESP_ERR_NVS_NOT_FOUND ? primary : backup;
}

template<class T>
esp_err_t read_exact_blob(nvs_handle_t handle, const char* key, T& output) {
    size_t length = 0;
    esp_err_t error = nvs_get_blob(handle, key, nullptr, &length);
    if (error != ESP_OK) return error;
    if (length != sizeof(T)) return ESP_ERR_NVS_INVALID_LENGTH;
    error = nvs_get_blob(handle, key, &output, &length);
    if (error != ESP_OK) return error;
    return length == sizeof(T) ? ESP_OK : ESP_ERR_NVS_INVALID_LENGTH;
}

template<class T, class Validator>
esp_err_t read_mirrored_blob(nvs_handle_t handle, const char* primary, const char* backup,
                            T& output, Validator valid) {
    auto read = [&](const char* key) {
        const esp_err_t error = read_exact_blob(handle, key, output);
        return error == ESP_OK && !valid(output) ? ESP_ERR_NVS_INVALID_STATE : error;
    };
    const esp_err_t error = read(primary);
    if (!recoverable_blob_error(error)) return error;
    return fallback_result(error, read(backup));
}

template<class T>
esp_err_t write_mirrored_blob(nvs_handle_t handle, const char* primary, const char* backup,
                             const T& value) {
    if (!handle) return ESP_ERR_INVALID_STATE;
    std::unique_ptr<T> verify(new (std::nothrow) T{});
    if (!verify) return ESP_ERR_NO_MEM;
    esp_err_t error = nvs_set_blob(handle, primary, &value, sizeof(T));
    if (error == ESP_OK) error = nvs_commit(handle);
    if (error != ESP_OK) return error;
    error = read_exact_blob(handle, primary, *verify);
    if (error != ESP_OK) return error;
    if (std::memcmp(verify.get(), &value, sizeof(T)) != 0) return ESP_ERR_NVS_INVALID_STATE;
    // Keep the previous backup until the new primary is committed and verified.
    // A backup failure is a partial save and must be reported to the caller.
    error = nvs_set_blob(handle, backup, &value, sizeof(T));
    return error == ESP_OK ? nvs_commit(handle) : error;
}

esp_err_t erase_keys(nvs_handle_t handle, std::initializer_list<const char*> keys) {
    if (!handle) return ESP_ERR_INVALID_STATE;
    bool changed = false;
    for (const char* key : keys) {
        const esp_err_t error = nvs_erase_key(handle, key);
        if (error == ESP_OK) changed = true;
        else if (error != ESP_ERR_NVS_NOT_FOUND) return error;
    }
    return changed ? nvs_commit(handle) : ESP_OK;
}

bool valid_scene(const SceneConfig& scene) {
    if (scene.schema_version != 2 || !terminated(scene.name)) return false;
    for (const auto& channel : scene.channels) {
        for (const auto& layer : channel.layers) {
            if (layer.effect > static_cast<uint8_t>(SignageEffect::Confetti) ||
                static_cast<uint8_t>(layer.blend_mode) > static_cast<uint8_t>(BlendMode::Screen)) return false;
        }
    }
    return true;
}

bool valid_motion(const SceneMotionConfig& motion) {
    if (motion.schema_version != 1 || motion.track_count > kAutomationTracksPerScene ||
        static_cast<uint8_t>(motion.transition_curve) > static_cast<uint8_t>(TransitionCurve::Step)) return false;
    for (uint8_t i = 0; i < motion.track_count; ++i) {
        const auto& track = motion.tracks[i];
        if (track.point_count > kAutomationPointsPerTrack || track.channel >= kChannelCount || track.layer >= 3 ||
            static_cast<uint8_t>(track.target) > static_cast<uint8_t>(AutomationTarget::LayerParameter) ||
            static_cast<uint8_t>(track.curve) > static_cast<uint8_t>(AutomationCurve::Step)) return false;
        for (uint8_t p = 1; p < track.point_count; ++p) {
            if (track.points[p].position < track.points[p - 1].position) return false;
        }
    }
    return true;
}

bool valid_cue_list(const CueList& list) {
    if (!terminated(list.name) || list.count > kMaxCuesPerList ||
        static_cast<uint8_t>(list.mode) > static_cast<uint8_t>(CueRunMode::Manual) ||
        static_cast<uint8_t>(list.end_action) > static_cast<uint8_t>(CueEndAction::RestoreFallback)) return false;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (!terminated(list.cues[i].label) || list.cues[i].scene_index >= kMaxPresets ||
            static_cast<uint8_t>(list.cues[i].curve) > static_cast<uint8_t>(TransitionCurve::Step)) return false;
    }
    return true;
}

bool valid_sequence(const Sequence& sequence) {
    for (const auto& track : sequence.tracks) {
        if (track.channel_id >= kChannelCount || track.num_steps > 16) return false;
        for (uint8_t i = 0; i < track.num_steps; ++i) {
            if (track.steps[i].preset_index >= kMaxPresets || track.steps[i].length_beats == 0) return false;
        }
    }
    return true;
}

bool valid_calendar(const CalendarSchedule& schedule) {
    for (const auto& entry : schedule.entries) {
        if (entry.hour > 23 || entry.minute > 59 || entry.days_of_week > 0x7f ||
            entry.preset_index >= kMaxPresets || entry.cue_list_index >= kMaxCueLists ||
            static_cast<uint8_t>(entry.action) > static_cast<uint8_t>(CalendarAction::CueList)) return false;
    }
    return true;
}

bool valid_network(const NetworkSettings& settings, bool allow_missing_ap_password = false) {
    if (settings.schema_version != 2 ||
        static_cast<uint8_t>(settings.mode) > static_cast<uint8_t>(NetworkMode::StaWithApFallback) ||
        settings.ap_channel < 1 || settings.ap_channel > 13 ||
        !terminated(settings.sta_ssid) || !terminated(settings.sta_password) ||
        !terminated(settings.ap_name) || !terminated(settings.ap_password)) return false;
    if (settings.mode != NetworkMode::ApOnly && settings.sta_ssid[0] == '\0') return false;
    const size_t sta_length = std::strlen(settings.sta_password);
    const size_t ap_length = std::strlen(settings.ap_password);
    return (sta_length == 0 || (sta_length >= 8 && sta_length <= 63)) &&
           ((ap_length >= 8 && ap_length <= 63) ||
            (ap_length == 0 && (allow_missing_ap_password || settings.mode == NetworkMode::StaOnly)));
}

} // namespace

esp_err_t ConfigStore::init() {
    if (handle_) return ESP_OK;
    esp_err_t err = NvsRuntime::initialise();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ConfigStore: NVS initialization failed (%s); recovery required", esp_err_to_name(err));
        return err;
    }
    ESP_RETURN_ON_ERROR(nvs_open(storage_namespace_, NVS_READWRITE, &handle_), TAG, "open");

    bool restore_active = true;
    err = read_restore_marker(restore_active);
    if (err != ESP_OK) {
        nvs_close(handle_);
        handle_ = 0;
        return err;
    }
    if (restore_active) {
        ESP_LOGW(TAG, "Interrupted restore; skipping defaults and scene migrations");
        return ESP_OK;
    }

    err = ensure_defaults();
    if (err != ESP_OK) {
        nvs_close(handle_);
        handle_ = 0;
        return err;
    }
    ESP_LOGI(TAG, "ConfigStore ready (%u scenes)", scene_count());
    return ESP_OK;
}


// ── Scenes (Product Grade) ──────────────────────────────────────────

esp_err_t ConfigStore::save_scene(uint8_t index, const SceneConfig& scene) {
    if (index >= kMaxPresets || !valid_scene(scene)) return ESP_ERR_INVALID_ARG;

    std::unique_ptr<SceneConfig> scratch(new (std::nothrow) SceneConfig(scene));
    if (!scratch) return ESP_ERR_NO_MEM;
    SceneConfig &to_save = *scratch;
    to_save.checksum = calc_scene_crc(to_save);

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
    return write_mirrored_blob(handle_, key, bak_key, to_save);
}

esp_err_t ConfigStore::load_scene(uint8_t index, SceneConfig& scene, bool persist_changes) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[12];
    make_key(key, sizeof(key), "scene", index);

    // Legacy scenes were zone-based. Resolve topology lazily only when an old
    // scene blob is actually encountered; normal v2 scene loads stay NVS-light.
    std::unique_ptr<DeviceConfig> topology_cfg;
    esp_err_t topology_error = ESP_OK;
    bool topology_checked = false;
    bool have_topology = false;
    bool migrated_legacy_scene = false;
    auto legacy_zone_for_channel = [&](uint8_t channel) -> uint8_t {
        if (!topology_checked) {
            topology_cfg.reset(new (std::nothrow) DeviceConfig{});
            topology_error = topology_cfg ? load_config(*topology_cfg, persist_changes) : ESP_ERR_NO_MEM;
            have_topology = (topology_error == ESP_OK);
            topology_checked = true;
        }
        if (have_topology && channel < 16) {
            const uint16_t bit = static_cast<uint16_t>(1U << channel);
            for (uint8_t z = 0; z < 8; ++z) {
                if ((topology_cfg->groups[z].channel_mask & bit) != 0) {
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
        if (err == ESP_OK && len == 0) return ESP_ERR_NVS_INVALID_LENGTH;
        if (err == ESP_OK && len > 0) {
            if (len == sizeof(SceneConfig)) {
                len = sizeof(SceneConfig);
                err = nvs_get_blob(handle_, k, &out, &len);
                if (err == ESP_OK) {
                    if (len != sizeof(out) || out.checksum != calc_scene_crc(out) || !valid_scene(out)) {
                        ESP_LOGW(TAG, "Scene %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    return ESP_OK;
                }
            } else if (len == sizeof(SceneConfigV1)) {
                std::unique_ptr<SceneConfigV1> v1(new (std::nothrow) SceneConfigV1{});
                if (!v1) return ESP_ERR_NO_MEM;
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
                    if (topology_error != ESP_OK) return topology_error;
                    out.checksum = calc_scene_crc(out);
                    if (!valid_scene(out)) return ESP_ERR_NVS_INVALID_STATE;
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
                    
                    if (topology_error != ESP_OK) return topology_error;
                    out.checksum = calc_scene_crc(out);
                    if (!valid_scene(out)) return ESP_ERR_NVS_INVALID_STATE;
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
    if (recoverable_blob_error(err)) {
        // Try backup
        char bak_key[16];
        snprintf(bak_key, sizeof(bak_key), "scene_b_%u", index);
        ESP_LOGW(TAG, "Restoring scene %u from backup slot", index);
        err = fallback_result(err, load_from_key(bak_key, scene));
    }

    if (err == ESP_OK && migrated_legacy_scene && persist_changes) {
        // Persist the channel-based representation immediately so later scene
        // playback never has to re-run legacy topology resolution.
        const esp_err_t persist_err = save_scene(index, scene);
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist migrated scene %u: %s", index, esp_err_to_name(persist_err));
            return persist_err;
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
    char motion_key[16], motion_backup[16];
    make_key(motion_key, sizeof(motion_key), "smot", index);
    snprintf(motion_backup, sizeof(motion_backup), "smot_b_%u", index);
    return erase_keys(handle_, {bak_key, motion_backup, motion_key, key});
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
    if (index >= kMaxPresets || !valid_motion(motion)) return ESP_ERR_INVALID_ARG;

    SceneMotionConfig to_save = motion;
    to_save.schema_version = 1;
    to_save.checksum = calc_motion_crc(to_save);

    char key[16];
    make_key(key, sizeof(key), "smot", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "smot_b_%u", index);
    return write_mirrored_blob(handle_, key, bak_key, to_save);
}

esp_err_t ConfigStore::load_scene_motion(uint8_t index, SceneMotionConfig& motion) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;
    char key[16], backup[16];
    make_key(key, sizeof(key), "smot", index);
    snprintf(backup, sizeof(backup), "smot_b_%u", index);
    const esp_err_t error = read_mirrored_blob(handle_, key, backup, motion,
        [](const SceneMotionConfig& value) {
            return value.checksum == calc_motion_crc(value) && valid_motion(value);
        });
    if (error != ESP_OK) clear_scene_motion(motion);
    return error;
}


esp_err_t ConfigStore::delete_scene_motion(uint8_t index) {
    if (index >= kMaxPresets) return ESP_ERR_INVALID_ARG;

    char key[16];
    make_key(key, sizeof(key), "smot", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "smot_b_%u", index);
    return erase_keys(handle_, {bak_key, key});
}

// ── Cue Lists ───────────────────────────────────────────────────────

esp_err_t ConfigStore::save_cue_list(uint8_t index, const CueList& list) {
    if (index >= kMaxCueLists || !valid_cue_list(list)) return ESP_ERR_INVALID_ARG;

    CueList to_save = list;
    to_save.checksum = calc_cue_list_crc(to_save);

    char key[16];
    make_key(key, sizeof(key), "cuel", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "cuel_b_%u", index);
    return write_mirrored_blob(handle_, key, bak_key, to_save);
}

esp_err_t ConfigStore::load_cue_list(uint8_t index, CueList& list) {
    if (index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;
    char key[16], backup[16];
    make_key(key, sizeof(key), "cuel", index);
    snprintf(backup, sizeof(backup), "cuel_b_%u", index);
    const esp_err_t error = read_mirrored_blob(handle_, key, backup, list,
        [](const CueList& value) {
            return value.checksum == calc_cue_list_crc(value) && valid_cue_list(value);
        });
    if (error != ESP_OK) clear_cue_list(list);
    return error;
}


esp_err_t ConfigStore::delete_cue_list(uint8_t index) {
    if (index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;

    char key[16];
    make_key(key, sizeof(key), "cuel", index);

    char bak_key[16];
    snprintf(bak_key, sizeof(bak_key), "cuel_b_%u", index);
    return erase_keys(handle_, {bak_key, key});
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
    if (config.schema_version != 4) add_error("Unsupported device schema %u", config.schema_version);
    if (!terminated(config.device_name) || !terminated(config.mqtt_broker) || !terminated(config.timezone) ||
        !terminated(config.admin_token) || !terminated(config.installer_token) || !terminated(config.operator_token) ||
        !terminated(config.serial_number) || !terminated(config.hardware_revision) ||
        !terminated(config.site_label) || !terminated(config.group_label)) {
        add_error("Device configuration contains an unterminated string");
    }
    if (config.active_preset_index >= kMaxPresets || config.fallback_preset_index >= kMaxPresets) {
        add_error("Device preset index is out of range");
    }
    if (config.sync_role > 2 || static_cast<uint8_t>(config.protocol.merge_mode) > static_cast<uint8_t>(MergeMode::LTP)) {
        add_error("Device mode is out of range");
    }
    if (config.gamma_x10 < 10 || config.gamma_x10 > 30) {
        add_error("gamma_x10 (%u) out of sane range [10, 30]", (unsigned)config.gamma_x10);
    }
    if (config.max_power_mw > 2000000) { // 2,000 W software ceiling
        add_error("max_power_mw (%u) exceeds sane limit 2000000", (unsigned)config.max_power_mw);
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

    // 3. Channels validation (LG-023 16-Channel Hardware Model & LG-HW-005 Power)
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const auto& c = config.channels[ch];
        if (!terminated(c.name)) add_error("channel[%u] name is unterminated", (unsigned)ch);
        if (!c.enabled) {
            continue;
        }

        const auto hw = channel_hardware(ch);
        if (hw.kind == ChannelKind::Addressable) {
            if (c.pixel_count > MAX_PIXELS) {
                add_error("channel[%u].pixel_count (%u) exceeds MAX_PIXELS (%u)",
                          (unsigned)ch, (unsigned)c.pixel_count, (unsigned)MAX_PIXELS);
            }
            if (!pixel_order_valid(c.addressable_type, c.pixel_order)) {
                add_error("channel[%u] invalid pixel order %s for addressable type %s",
                          (unsigned)ch, pixel_order_name(c.pixel_order),
                          c.addressable_type == AddressableType::WS2812 ? "WS2812" : "SK6812");
            }
        }

        if (c.channel_limit_mw > 1000000) {
            add_error("channel[%u].channel_limit_mw (%u) exceeds 1000000", (unsigned)ch, (unsigned)c.channel_limit_mw);
        }
        if (c.rated_full_power_mw > 1000000) {
            add_error("channel[%u].rated_full_power_mw (%u) exceeds 1000000", (unsigned)ch, (unsigned)c.rated_full_power_mw);
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

    // 4. Power supply domains validation (LG-HW-005.5)
    for (const auto& group : config.groups) {
        if (!terminated(group.name) || static_cast<uint8_t>(group.role) > static_cast<uint8_t>(ZoneRole::MainLight)) {
            add_error("Channel group name or role is invalid");
        }
    }
    uint16_t combined_supply_mask = 0;
    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& psu = config.power_supplies[s];
        if (!terminated(psu.name)) add_error("power_supplies[%u] name is unterminated", (unsigned)s);
        if (!psu.enabled) continue;

        if (psu.voltage_mv < 3000 || psu.voltage_mv > 60000) {
            add_error("power_supplies[%u].voltage_mv (%u) out of range [3000, 60000]", (unsigned)s, (unsigned)psu.voltage_mv);
        }
        if (psu.rated_current_ma == 0 || psu.rated_current_ma > 200000) {
            add_error("power_supplies[%u].rated_current_ma (%u) out of range [1, 200000]", (unsigned)s, (unsigned)psu.rated_current_ma);
        }
        if (psu.usable_capacity_permille > 1000) {
            add_error("power_supplies[%u].usable_capacity_permille (%u) exceeds 1000", (unsigned)s, (unsigned)psu.usable_capacity_permille);
        }

        // Check for supply assignment conflicts
        if ((combined_supply_mask & psu.channel_mask) != 0) {
            add_error("power_supplies[%u] channel_mask (0x%04X) conflicts with existing assignment (0x%04X)",
                      (unsigned)s, (unsigned)psu.channel_mask, (unsigned)combined_supply_mask);
        }
        combined_supply_mask |= psu.channel_mask;
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

    std::unique_ptr<DeviceConfig> to_save(new (std::nothrow) DeviceConfig(config));
    std::unique_ptr<DeviceConfig> verify_buf(new (std::nothrow) DeviceConfig{});
    if (!to_save || !verify_buf) {
        persistence_state_ = ConfigPersistenceState::SaveFailed;
        return ESP_ERR_NO_MEM;
    }
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

bool ConfigStore::migrate_v2_to_v3(const DeviceConfigV2& v2, DeviceConfigV3& v3, uint32_t* out_warning_count) {
    v3 = DeviceConfigV3{};
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
    for (uint8_t ch = 0; ch < 16; ++ch) {
        v3.channels[ch] = ChannelConfigV3{};
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

bool ConfigStore::migrate_v3_to_v4(const DeviceConfigV3& v3, DeviceConfig& v4, uint32_t* out_warning_count) {
    v4.reset();
    v4.schema_version = 4;
    safe_strncpy(v4.device_name, v3.device_name, sizeof(v4.device_name));
    v4.active_preset_index = v3.active_preset_index;
    v4.fallback_preset_index = v3.fallback_preset_index;
    v4.watchdog_timeout_s = v3.watchdog_timeout_s;
    v4.max_power_mw = static_cast<PowerMilliwatts>(v3.max_power_mw);
    v4.gamma_x10 = v3.gamma_x10;
    v4.soft_start_enabled = v3.soft_start_enabled;
    v4.soft_start_ms = v3.soft_start_ms;
    v4.mqtt_enabled = v3.mqtt_enabled;
    safe_strncpy(v4.mqtt_broker, v3.mqtt_broker, sizeof(v4.mqtt_broker));
    v4.mqtt_port = v3.mqtt_port;
    safe_strncpy(v4.timezone, v3.timezone, sizeof(v4.timezone));
    v4.sync_enabled = v3.sync_enabled;
    v4.sync_role = v3.sync_role;
    v4.sync_token = v3.sync_token;

    safe_strncpy(v4.admin_token, v3.admin_token, sizeof(v4.admin_token));
    safe_strncpy(v4.installer_token, v3.installer_token, sizeof(v4.installer_token));
    safe_strncpy(v4.operator_token, v3.operator_token, sizeof(v4.operator_token));

    safe_strncpy(v4.serial_number, v3.serial_number, sizeof(v4.serial_number));
    safe_strncpy(v4.hardware_revision, v3.hardware_revision, sizeof(v4.hardware_revision));
    safe_strncpy(v4.site_label, v3.site_label, sizeof(v4.site_label));
    safe_strncpy(v4.group_label, v3.group_label, sizeof(v4.group_label));

    // Migrate channels: separate rated full load from channel budget (HW-005.8)
    for (uint8_t ch = 0; ch < 16; ++ch) {
        const auto& c3 = v3.channels[ch];
        auto& c4 = v4.channels[ch];

        safe_strncpy(c4.name, c3.name, sizeof(c4.name));
        c4.enabled = c3.enabled;
        c4.addressable_type = c3.addressable_type;
        c4.pixel_order = c3.pixel_order;
        c4.pixel_count = c3.pixel_count;
        c4.max_brightness = c3.max_brightness;
        c4.gamma_correction = c3.gamma_correction;

        c4.max_milliwatts = static_cast<uint32_t>(c3.max_milliwatts);
        if (ch < 8) {
            // Addressable channels: max_milliwatts was a channel limit; full power is calculated from pixel model
            c4.channel_limit_mw = static_cast<uint32_t>(c3.max_milliwatts);
            c4.rated_full_power_mw = 0;
        } else {
            // PWM channels: legacy max_milliwatts served as both full rating and channel budget
            c4.rated_full_power_mw = static_cast<uint32_t>(c3.max_milliwatts);
            c4.channel_limit_mw = static_cast<uint32_t>(c3.max_milliwatts);
        }

        c4.dmx_universe = c3.dmx_universe;
        c4.dmx_start_channel = c3.dmx_start_channel;
        c4.dmx_mode = c3.dmx_mode;
    }

    for (uint8_t g = 0; g < 8; ++g) {
        v4.groups[g] = v3.groups[g];
    }
    v4.protocol = v3.protocol;

    // Zero-initialize power supplies (none enabled by default in legacy migration)
    for (uint8_t p = 0; p < kMaxPowerSupplies; ++p) {
        v4.power_supplies[p] = PowerSupplyConfig{};
    }

    v4.checksum = calc_device_crc(v4);
    return true;
}

esp_err_t ConfigStore::load_config(DeviceConfig& config, bool persist_changes) {
    bool needs_persist = false;

    auto load_from_key = [&](const char* k, DeviceConfig& out, bool& migrated) -> esp_err_t {
        migrated = false;
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err == ESP_OK && len == 0) return ESP_ERR_NVS_INVALID_LENGTH;
        if (err == ESP_OK && len > 0) {
            if (len == sizeof(DeviceConfig)) {
                len = sizeof(DeviceConfig);
                err = nvs_get_blob(handle_, k, &out, &len);
                if (err == ESP_OK) {
                    if (len != sizeof(out) || out.checksum != calc_device_crc(out)) {
                        ESP_LOGW(TAG, "DeviceConfig %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    if (out.schema_version == 0 || out.schema_version > 4) return ESP_ERR_INVALID_VERSION;
                    if (out.schema_version < 4) {
                        ESP_LOGI(TAG, "Migrating DeviceConfig %s from in-memory schema v%u to v4", k, out.schema_version);
                        out.schema_version = 4;
                        out.checksum = calc_device_crc(out);
                        migrated = true;
                    }
                    if (validate_config(out) != ESP_OK) return ESP_ERR_NVS_INVALID_STATE;
                    return ESP_OK;
                }
            } else if (len == sizeof(DeviceConfigV3)) {
                std::unique_ptr<DeviceConfigV3> v3(new (std::nothrow) DeviceConfigV3{});
                if (!v3) return ESP_ERR_NO_MEM;
                err = nvs_get_blob(handle_, k, v3.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v3.get(), sizeof(DeviceConfigV3) - sizeof(uint32_t));
                    if (v3->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy DeviceConfigV3 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    ESP_LOGI(TAG, "Migrating legacy v3 blob %s to v4", k);
                    migrate_v3_to_v4(*v3, out);
                    if (validate_config(out) != ESP_OK) return ESP_ERR_NVS_INVALID_STATE;
                    migrated = true;
                    return ESP_OK;
                }
            } else if (len == sizeof(DeviceConfigV2)) {
                std::unique_ptr<DeviceConfigV2> v2(new (std::nothrow) DeviceConfigV2{});
                if (!v2) return ESP_ERR_NO_MEM;
                err = nvs_get_blob(handle_, k, v2.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v2.get(), sizeof(DeviceConfigV2) - sizeof(uint32_t));
                    if (v2->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy DeviceConfigV2 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    ESP_LOGI(TAG, "Migrating legacy v2 blob %s through v3 to v4", k);
                    std::unique_ptr<DeviceConfigV3> v3(new (std::nothrow) DeviceConfigV3{});
                    if (!v3) return ESP_ERR_NO_MEM;
                    migrate_v2_to_v3(*v2, *v3);
                    migrate_v3_to_v4(*v3, out);
                    if (validate_config(out) != ESP_OK) return ESP_ERR_NVS_INVALID_STATE;
                    migrated = true;
                    return ESP_OK;
                }
            } else if (len == sizeof(DeviceConfigV1)) {
                std::unique_ptr<DeviceConfigV1> v1(new (std::nothrow) DeviceConfigV1{});
                if (!v1) return ESP_ERR_NO_MEM;
                err = nvs_get_blob(handle_, k, v1.get(), &len);
                if (err == ESP_OK) {
                    uint32_t expected_crc = esp_rom_crc32_le(0, (const uint8_t*)v1.get(), sizeof(DeviceConfigV1) - sizeof(uint32_t));
                    if (v1->checksum != expected_crc) {
                        ESP_LOGW(TAG, "Legacy DeviceConfigV1 %s CRC mismatch", k);
                        return ESP_ERR_NVS_INVALID_STATE;
                    }
                    ESP_LOGI(TAG, "Migrating legacy v1 blob %s through v2 and v3 to v4", k);
                    std::unique_ptr<DeviceConfigV2> v2(new (std::nothrow) DeviceConfigV2{});
                    std::unique_ptr<DeviceConfigV3> v3(new (std::nothrow) DeviceConfigV3{});
                    if (!v2 || !v3) return ESP_ERR_NO_MEM;
                    migrate_v1_to_v2(*v1, *v2);
                    migrate_v2_to_v3(*v2, *v3);
                    migrate_v3_to_v4(*v3, out);
                    if (validate_config(out) != ESP_OK) return ESP_ERR_NVS_INVALID_STATE;
                    migrated = true;
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

    esp_err_t err = load_from_key("device_cfg", config, needs_persist);
    if (err == ESP_OK) {
        primary_valid_ = true;
        std::unique_ptr<DeviceConfig> bak_cfg(new (std::nothrow) DeviceConfig{});
        if (!bak_cfg) return ESP_ERR_NO_MEM;
        bool backup_migrated = false;
        esp_err_t bak_err = load_from_key("device_cfg_bak", *bak_cfg, backup_migrated);
        if (bak_err == ESP_ERR_NO_MEM) return bak_err;
        if (bak_err == ESP_OK && bak_cfg->checksum == config.checksum) {
            backup_valid_ = true;
            persistence_state_ = ConfigPersistenceState::Synchronized;
        } else {
            backup_valid_ = false;
            persistence_state_ = ConfigPersistenceState::PrimaryOnly;
        }
    } else if (recoverable_blob_error(err)) {
        primary_valid_ = false;
        ESP_LOGW(TAG, "Restoring DeviceConfig from backup slot");
        err = fallback_result(err, load_from_key("device_cfg_bak", config, needs_persist));
        if (err == ESP_OK) {
            backup_valid_ = true;
            persistence_state_ = ConfigPersistenceState::BackupOnly;
        } else {
            backup_valid_ = false;
            persistence_state_ = ConfigPersistenceState::RecoveryRequired;
        }
    } else {
        primary_valid_ = false;
        backup_valid_ = false;
        persistence_state_ = ConfigPersistenceState::RecoveryRequired;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND && persist_changes) {
        config.reset();
        ESP_LOGI(TAG, "Default credentials active (Admin role configured)");
        populate_default_topology(config);
        config.schema_version = 4;
        config.checksum = calc_device_crc(config);
        return save_config(config);
    } else if (err == ESP_OK) {
        if (needs_persist && persist_changes) {
            ESP_LOGI(TAG, "Persisting migrated/updated DeviceConfig to NVS");
            esp_err_t s_err = save_config(config);
            if (s_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to persist migrated DeviceConfig to NVS: %s", esp_err_to_name(s_err));
                return s_err;
            }

            std::unique_ptr<DeviceConfig> verify_cfg(new (std::nothrow) DeviceConfig{});
            if (!verify_cfg) return ESP_ERR_NO_MEM;
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
    if (!valid_sequence(seq)) return ESP_ERR_INVALID_ARG;
    std::unique_ptr<Sequence> to_save(new (std::nothrow) Sequence(seq));
    if (!to_save) return ESP_ERR_NO_MEM;
    to_save->enabled = false; // LG-033: Deprecated compatibility state, new saves always false
    to_save->checksum = calc_seq_crc(*to_save);

    return write_mirrored_blob(handle_, "seq_main", "seq_main_bak", *to_save);
}

esp_err_t ConfigStore::load_sequence(Sequence& seq, bool* was_present, bool persist_changes) {
    if (was_present) *was_present = false;
    bool migrated = false;

    auto load_from_key = [&](const char* k, Sequence& out) -> esp_err_t {
        size_t len = 0;
        esp_err_t err = nvs_get_blob(handle_, k, nullptr, &len);
        if (err != ESP_OK) return err;
        if (len == 0) return ESP_ERR_NVS_INVALID_LENGTH;

        if (len == sizeof(Sequence)) {
            len = sizeof(Sequence);
            err = nvs_get_blob(handle_, k, &out, &len);
            if (err != ESP_OK) return err;
            if (len != sizeof(out) || out.checksum != calc_seq_crc(out)) {
                ESP_LOGW(TAG, "Sequence %s CRC mismatch", k);
                return ESP_ERR_NVS_INVALID_STATE;
            }

            // LG-033: Verify canonical 16 physical tracks (track 0 -> A1 .. track 15 -> P8)
            bool needs_canonical = false;
            for (uint8_t i = 0; i < 16; ++i) {
                if (out.tracks[i].channel_id != i) {
                    needs_canonical = true;
                    break;
                }
            }
            if (needs_canonical) {
                Sequence canonical{};
                canonical.bpm = out.bpm > 0 ? out.bpm : 120;
                canonical.enabled = false;
                for (uint8_t i = 0; i < 16; ++i) {
                    canonical.tracks[i].channel_id = i;
                    canonical.tracks[i].num_steps = 0;
                    canonical.tracks[i].enabled = false;
                }
                bool mapped[16] = {};
                for (uint8_t i = 0; i < 16; ++i) {
                    uint8_t ch = out.tracks[i].channel_id;
                    if (ch < 16) {
                        if (!mapped[ch]) {
                            canonical.tracks[ch] = out.tracks[i];
                            canonical.tracks[ch].channel_id = ch;
                            mapped[ch] = true;
                        } else {
                            ESP_LOGW(TAG, "Sequence has duplicate channel %u; first valid wins", ch);
                        }
                    }
                }
                out = canonical;
                out.checksum = calc_seq_crc(out);
                migrated = true;
            }
            out.enabled = false; // LG-033: Deprecate Sequence.enabled as runtime authority
            out.checksum = calc_seq_crc(out);
            if (!valid_sequence(out)) return ESP_ERR_NVS_INVALID_STATE;
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

            // LG-033: Map old tracks to canonical physical tracks by channel_id
            out = Sequence{};
            for (uint8_t i = 0; i < 16; ++i) {
                out.tracks[i] = SequencerTrack{};
                out.tracks[i].channel_id = i;
                out.tracks[i].num_steps = 0;
                out.tracks[i].enabled = false;
            }
            bool mapped[16] = {};
            for (uint8_t i = 0; i < 8; ++i) {
                uint8_t ch = old.tracks[i].channel_id;
                if (ch < 16) {
                    if (!mapped[ch]) {
                        out.tracks[ch] = old.tracks[i];
                        out.tracks[ch].channel_id = ch;
                        mapped[ch] = true;
                    } else {
                        ESP_LOGW(TAG, "Legacy sequence has duplicate channel %u; first valid wins", ch);
                    }
                }
            }
            out.bpm = old.bpm > 0 ? old.bpm : 120;
            out.enabled = false;
            out.checksum = calc_seq_crc(out);
            if (!valid_sequence(out)) return ESP_ERR_NVS_INVALID_STATE;
            migrated = true;
            ESP_LOGI(TAG, "Migrated sequence %s from 8 tracks to 16 canonical physical tracks", k);
            return ESP_OK;
        }

        return ESP_ERR_NVS_INVALID_LENGTH;
    };

    esp_err_t err = load_from_key("seq_main", seq);
    if (recoverable_blob_error(err)) {
        ESP_LOGW(TAG, "Restoring Sequence from backup slot");
        err = fallback_result(err, load_from_key("seq_main_bak", seq));
    }

    if (err == ESP_OK && migrated && persist_changes) {
        const esp_err_t persist_err = save_sequence(seq);
        if (persist_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist migrated 16-track sequence: %s", esp_err_to_name(persist_err));
            return persist_err;
        }
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        seq = Sequence{};
        for (uint8_t i = 0; i < 16; ++i) {
            seq.tracks[i].channel_id = i;
            seq.tracks[i].num_steps = 0;
            seq.tracks[i].enabled = false;
        }
        seq.enabled = false;
        seq.checksum = calc_seq_crc(seq);
        return ESP_OK;
    }
    if (err == ESP_OK && was_present) *was_present = true;
    return err;
}

esp_err_t ConfigStore::delete_sequence() {
    return erase_keys(handle_, {"seq_main_bak", "seq_main"});
}

// ── Calendar scheduling ────────────────────────────────────────────────

esp_err_t ConfigStore::save_calendar_schedule(const CalendarSchedule& schedule) {
    if (!valid_calendar(schedule)) return ESP_ERR_INVALID_ARG;
    std::unique_ptr<CalendarSchedule> to_save(new (std::nothrow) CalendarSchedule(schedule));
    if (!to_save) return ESP_ERR_NO_MEM;
    to_save->checksum = calc_calendar_crc(*to_save);

    return write_mirrored_blob(handle_, "calendar", "calendar_bak", *to_save);
}

esp_err_t ConfigStore::load_calendar_schedule(CalendarSchedule& schedule, bool* was_present) {
    if (was_present) *was_present = false;
    const esp_err_t error = read_mirrored_blob(handle_, "calendar", "calendar_bak", schedule,
        [](const CalendarSchedule& value) {
            return value.checksum == calc_calendar_crc(value) && valid_calendar(value);
        });
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        schedule = CalendarSchedule{};
        schedule.checksum = calc_calendar_crc(schedule);
        return ESP_OK;
    }
    if (error == ESP_OK && was_present) *was_present = true;
    return error;
}

esp_err_t ConfigStore::delete_calendar_schedule() {
    return erase_keys(handle_, {"calendar_bak", "calendar"});
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
    to_save.schema_version = 2;
    if (!valid_network(to_save)) return ESP_ERR_INVALID_ARG;
    to_save.checksum = calc_network_settings_crc(to_save);
    return write_mirrored_blob(handle_, "network", "network_b", to_save);
}

esp_err_t ConfigStore::load_network_settings(NetworkSettings& settings) {
    if (!handle_) return ESP_ERR_INVALID_STATE;
    NetworkSettings candidate{};
    bool migrated = false;
    auto load_from_key = [&](const char* key) -> esp_err_t {
        migrated = false;
        size_t length = 0;
        esp_err_t error = nvs_get_blob(handle_, key, nullptr, &length);
        if (error != ESP_OK) return error;
        if (length == sizeof(NetworkSettings)) {
            error = read_exact_blob(handle_, key, candidate);
            if (error != ESP_OK) return error;
            if (candidate.checksum != calc_network_settings_crc(candidate)) {
                return ESP_ERR_NVS_INVALID_STATE;
            }
            if (candidate.schema_version != 1 && candidate.schema_version != 2) return ESP_ERR_INVALID_VERSION;
            // Earlier firmware wrote the v2 layout with a v1 tag. Its v2 CRC
            // and exact size distinguish it from a true legacy v1 record.
            migrated = candidate.schema_version == 1;
            candidate.schema_version = 2;
        } else if (length == sizeof(NetworkSettingsV1)) {
            NetworkSettingsV1 old{};
            error = read_exact_blob(handle_, key, old);
            if (error != ESP_OK) return error;
            if (old.checksum != calc_network_settings_v1_crc(old) ||
                !terminated(old.sta_ssid) || !terminated(old.sta_password) || !terminated(old.ap_name)) {
                return ESP_ERR_NVS_INVALID_STATE;
            }
            if (old.schema_version != 1) return ESP_ERR_INVALID_VERSION;
            candidate = NetworkSettings{};
            candidate.mode = old.mode;
            safe_strncpy(candidate.sta_ssid, old.sta_ssid, sizeof(candidate.sta_ssid));
            safe_strncpy(candidate.sta_password, old.sta_password, sizeof(candidate.sta_password));
            safe_strncpy(candidate.ap_name, old.ap_name, sizeof(candidate.ap_name));
            candidate.ap_channel = old.ap_channel;
            migrated = true;
        } else {
            return ESP_ERR_NVS_INVALID_LENGTH;
        }
        if (!valid_network(candidate, true)) return ESP_ERR_NVS_INVALID_STATE;
        if (candidate.ap_password[0] == '\0') {
            generate_random_ap_password(candidate.ap_password, sizeof(candidate.ap_password));
            migrated = true;
        }
        candidate.checksum = calc_network_settings_crc(candidate);
        return ESP_OK;
    };

    esp_err_t error = load_from_key("network");
    if (recoverable_blob_error(error)) error = fallback_result(error, load_from_key("network_b"));
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        settings = NetworkSettings{};
        generate_random_ap_password(settings.ap_password, sizeof(settings.ap_password));
        settings.checksum = calc_network_settings_crc(settings);
        return ESP_ERR_NOT_FOUND;
    }
    if (error != ESP_OK) return error;
    if (migrated) {
        error = save_network_settings(candidate);
        if (error != ESP_OK) return error;
    }
    settings = candidate;
    return ESP_OK;
}

// ── Defaults ────────────────────────────────────────────────────────

esp_err_t ConfigStore::ensure_defaults() {
    std::unique_ptr<SceneConfig> scratch(new (std::nothrow) SceneConfig{});
    if (!scratch) return ESP_ERR_NO_MEM;
    esp_err_t invalid = ESP_OK;
    for (uint8_t index = 0; index < kMaxPresets; ++index) {
        const esp_err_t load_error = load_scene(index, *scratch);
        if (load_error == ESP_OK) return ESP_OK;
        if (!recoverable_blob_error(load_error)) return load_error;
        if (load_error != ESP_ERR_NVS_NOT_FOUND) invalid = load_error;
    }
    // Defaults are only for empty storage. Preserve existing corrupt records
    // for recovery, and never overwrite scene 1 just because scene 0 is absent.
    if (invalid != ESP_OK) return invalid;

    ESP_LOGI(TAG, "No scenes found, creating defaults...");

    // ── Product-Grade Default Scenes ──
    SceneConfig *s_open = scratch.get();
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
        return err;
    }

    SceneConfig *s_night = scratch.get();
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
        return err;
    }
    return ESP_OK;
}

uint32_t ConfigStore::compute_device_crc(const DeviceConfig& dc) {
    return calc_device_crc(dc);
}

esp_err_t ConfigStore::read_restore_marker(bool& in_progress, uint32_t* out_checksum) {
    in_progress = true; // Failure is unsafe to interpret as an idle restore.
    if (out_checksum) *out_checksum = 0;
    if (!handle_) return ESP_ERR_INVALID_STATE;
    uint8_t flag = 0;
    esp_err_t error = nvs_get_u8(handle_, "rst_prog", &flag);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        in_progress = false;
        return ESP_OK;
    }
    if (error != ESP_OK) return error;
    if (flag > 1) return ESP_ERR_NVS_INVALID_STATE;
    if (flag == 0) {
        in_progress = false;
        return ESP_OK;
    }
    uint32_t checksum = 0;
    error = nvs_get_u32(handle_, "rst_crc", &checksum);
    if (error != ESP_OK) return error;
    if (out_checksum) *out_checksum = checksum;
    return ESP_OK;
}

bool ConfigStore::is_restore_in_progress(uint32_t* out_checksum) {
    bool in_progress = true;
    return read_restore_marker(in_progress, out_checksum) != ESP_OK || in_progress;
}

esp_err_t ConfigStore::set_restore_in_progress(bool in_progress, uint32_t package_checksum) {
    if (!handle_) return ESP_ERR_INVALID_STATE;
    if (in_progress) {
        // Commit the identifying checksum before publishing an active marker.
        esp_err_t error = nvs_set_u32(handle_, "rst_crc", package_checksum);
        if (error == ESP_OK) error = nvs_commit(handle_);
        if (error == ESP_OK) error = nvs_set_u8(handle_, "rst_prog", 1);
        return error == ESP_OK ? nvs_commit(handle_) : error;
    }

    bool was_active = true;
    uint32_t old_checksum = 0;
    esp_err_t error = read_restore_marker(was_active, &old_checksum);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle_, "rst_prog", 0);
    if (error == ESP_OK) error = nvs_commit(handle_);
    if (error == ESP_OK) {
        error = nvs_erase_key(handle_, "rst_crc");
        if (error == ESP_OK) error = nvs_commit(handle_);
        else if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    }
    if (error != ESP_OK && was_active) {
        // NVS writes can be visible before commit returns. Keep recovery active
        // after an unsuccessful clear, including a failed checksum cleanup.
        const esp_err_t checksum_error = nvs_set_u32(handle_, "rst_crc", old_checksum);
        const esp_err_t marker_error = nvs_set_u8(handle_, "rst_prog", 1);
        const esp_err_t commit_error = nvs_commit(handle_);
        if (checksum_error != ESP_OK || marker_error != ESP_OK || commit_error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to re-arm restore marker after clear failure");
        }
    }
    return error;
}

} // namespace lg

