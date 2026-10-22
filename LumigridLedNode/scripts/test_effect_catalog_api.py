"""Compile the real catalogue, scene parsers and request reader on the host.

FactoryApi.cpp also owns ESP-IDF networking and hardware services. Extract only
these self-contained function bodies so the test exercises production code,
with HTTP I/O and heap admission stubbed at the boundary, without requiring the
ESP32 toolchain. The real cJSON parser still handles every request body.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/factory/FactoryApi.cpp"
CJSON = ROOT / "managed_components/espressif__cjson/cJSON"


def main():
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cxx or not cc:
        raise SystemExit("A host C and C++ compiler is required")
    source = SOURCE.read_text()
    limits = source[source.index("constexpr size_t kMaxBodyBytes"):
                    source.index("esp_err_t send_json(")]
    reader = source[source.index("esp_err_t read_json("):
                    source.index("bool json_u8(")]
    parser = source[source.index("static bool parse_and_validate_layer_obj("):
                    source.index("esp_err_t put_preset(")]
    handlers = source[source.index("esp_err_t get_effects("):
                      source.index("esp_err_t get_calendar_schedule(")]
    preset_handler = source[source.index("esp_err_t put_preset("):
                            source.index("esp_err_t get_scene_motion(")]
    assert "read_json(request, &root, kMaxPresetBodyBytes)" in preset_handler
    # A full current scene: 48 layers, four six-point motion tracks, longest
    # effect/blend names and numeric fields, plus maximally escaped scene name.
    layer = dict(effect="staticpulse", blendMode="multiply",
                 primaryColor=dict(r=255, g=255, b=255),
                 secondaryColor=dict(r=255, g=255, b=255), speedMs=60000,
                 fxParam=100, paletteId=13, opacity=255, enabled=False)
    track = dict(target="layerSpeed", channel=15, layer=2, curve="easeInOut",
                 loop=False, periodMs=3600000,
                 points=[dict(position=p, value=60000)
                         for p in (10000, 20000, 30000, 40000, 50000, 65535)])
    scene = dict(index=11, name="\x01" * 23, globalBrightness=255,
                 transitionMs=60000, channels=[dict(layers=[layer] * 3)] * 16,
                 motion=dict(transitionCurve="easeInOut", tracks=[track] * 4),
                 expectedSceneChecksum=4294967295, expectedMotionChecksum=4294967295)
    scene_json = json.dumps(scene, separators=(",", ":"))
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include "cJSON.h"
#include "SignageTypes.hpp"

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
              ESP_ERR_INVALID_SIZE = 0x104, ESP_ERR_INVALID_ARG = 0x102,
              ESP_ERR_TIMEOUT = 0x107, HTTPD_SOCK_ERR_TIMEOUT = -3;
struct httpd_req_t {
    std::string body;
    std::string status;
    bool authorized = true;
    int writes = 0;
    int fail_at = -1;
    std::string input;
    size_t content_len = 0, read_offset = 0;
};
int httpd_req_recv(httpd_req_t* r, char* output, size_t size) {
    // Deliberately fragment reads so the production loop must accumulate them.
    size = std::min({size, r->input.size() - r->read_offset, size_t{127}});
    std::memcpy(output, r->input.data() + r->read_offset, size);
    r->read_offset += size;
    return static_cast<int>(size);
}
int httpd_resp_set_status(httpd_req_t* r, const char* value) { r->status = value; return ESP_OK; }
int httpd_resp_set_type(httpd_req_t*, const char*) { return ESP_OK; }
int httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
int httpd_resp_send_chunk(httpd_req_t* r, const char* text, size_t size) {
    if (r->writes++ == r->fail_at) return ESP_FAIL;
    if (text) r->body.append(text, size);
    return ESP_OK;
}
int httpd_resp_sendstr(httpd_req_t* r, const char* text) {
    return httpd_resp_send_chunk(r, text, strlen(text));
}
namespace lg {
namespace api {
enum class ApiRole { Operator };
bool authorize(httpd_req_t* r, ApiRole) { return r->authorized; }
}
void note_api_allocation_failure() {}
cJSON* parse_admitted_json(const char* body, size_t length) {
    return cJSON_ParseWithLength(body, length);
}
int send_json(httpd_req_t* r, const char* body, const char* status = "200 OK") {
    r->status = status;
    return httpd_resp_sendstr(r, body);
}
int send_error(httpd_req_t* r, const char* status, const char* code) {
    r->status = status;
    r->body = std::string("{\"error\":\"") + code + "\"}";
    return ESP_OK;
}
''' + limits + reader + parser + handlers + r'''
} // namespace lg

size_t live_bytes = 0, peak_bytes = 0;
int allocations_left = -1;
void* tracked_malloc(size_t size) {
    if (allocations_left == 0) return nullptr;
    if (allocations_left > 0) --allocations_left;
    auto* block = static_cast<size_t*>(std::malloc(size + sizeof(size_t)));
    if (!block) return nullptr;
    *block = size;
    live_bytes += size;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return block + 1;
}
void tracked_free(void* ptr) {
    if (!ptr) return;
    auto* block = static_cast<size_t*>(ptr) - 1;
    live_bytes -= *block;
    std::free(block);
}

bool accepts(const char* text) {
    cJSON* json = cJSON_Parse(text);
    assert(json);
    lg::LayerConfig layer;
    const char* error = nullptr;
    bool result = lg::parse_and_validate_layer_obj(json, layer, error);
    cJSON_Delete(json);
    return result;
}

int main() {
    using namespace lg;
    const std::string full_scene = R"SCENE(''' + scene_json + r''')SCENE";
    assert(full_scene.size() > kMaxBodyBytes && full_scene.size() < kMaxPresetBodyBytes);
    httpd_req_t incoming;
    incoming.input = full_scene;
    incoming.content_len = incoming.input.size();
    cJSON* saved_scene = nullptr;
    assert(read_json(&incoming, &saved_scene) == ESP_ERR_INVALID_SIZE);
    assert(incoming.read_offset == 0 && saved_scene == nullptr);
    assert(read_json(&incoming, &saved_scene, kMaxPresetBodyBytes) == ESP_OK);
    const auto* channels = cJSON_GetObjectItemCaseSensitive(saved_scene, "channels");
    assert(cJSON_GetArraySize(channels) == 16);
    const char* validation_error = nullptr;
    for (int channel = 0; channel < 16; ++channel) {
        const auto* layers = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(channels, channel), "layers");
        assert(cJSON_GetArraySize(layers) == 3);
        for (int index = 0; index < 3; ++index) {
            LayerConfig layer;
            assert(parse_and_validate_layer_obj(cJSON_GetArrayItem(layers, index), layer, validation_error));
        }
    }
    SceneMotionConfig motion;
    assert(parse_and_validate_motion_json(cJSON_GetObjectItemCaseSensitive(saved_scene, "motion"),
                                         11, motion, validation_error));
    assert(motion.track_count == 4 && motion.tracks[3].point_count == 6);
    cJSON_Delete(saved_scene);
    incoming = {};
    incoming.input = full_scene + std::string(kMaxPresetBodyBytes - full_scene.size(), ' ');
    incoming.content_len = incoming.input.size();
    saved_scene = nullptr;
    assert(read_json(&incoming, &saved_scene, kMaxPresetBodyBytes) == ESP_OK);
    cJSON_Delete(saved_scene);
    incoming.read_offset = 0;
    incoming.input += ' ';
    incoming.content_len = incoming.input.size();
    saved_scene = nullptr;
    assert(read_json(&incoming, &saved_scene, kMaxPresetBodyBytes) == ESP_ERR_INVALID_SIZE);
    assert(incoming.read_offset == 0 && saved_scene == nullptr);
    for (unsigned id = 0; id < kSignagePaletteCount; ++id) {
        char body[64];
        snprintf(body, sizeof(body), "{\"effect\":\"plasma\",\"paletteId\":%u}", id);
        assert(accepts(body) && "Every advertised palette must save successfully");
    }
    assert(!accepts("{\"paletteId\":-1}"));
    assert(!accepts("{\"paletteId\":14}"));
    assert(!accepts("{\"paletteId\":1.5}"));
    assert(!accepts("{\"paletteId\":\"6\"}"));
    assert(!accepts("{\"effect\":1.5}"));
    assert(!accepts("{\"effect\":24}"));
    for (unsigned id = 0; id < kSignageEffectCount; ++id) {
        char body[80];
        snprintf(body, sizeof(body), "{\"effect\":\"%s\"}", signage_effect_name(static_cast<SignageEffect>(id)));
        assert(accepts(body));
        snprintf(body, sizeof(body), "{\"effect\":%u}", id);
        assert(accepts(body));
    }
    cJSON_Hooks hooks{tracked_malloc, tracked_free};
    cJSON_InitHooks(&hooks);
    for (bool effects : {true, false}) {
        httpd_req_t request;
        peak_bytes = 0;
        auto handler = effects ? get_effects : get_palettes;
        assert(handler(&request) == ESP_OK);
        assert(live_bytes == 0 && "Catalogue response leaked cJSON allocations");
        assert(peak_bytes < 8192 && "Catalogue must not allocate a whole growing JSON tree");
        cJSON* json = cJSON_Parse(request.body.c_str());
        assert(cJSON_IsArray(json));
        const unsigned count = effects ? kSignageEffectCount : kSignagePaletteCount;
        assert(cJSON_GetArraySize(json) == static_cast<int>(count));
        for (unsigned id = 0; id < count; ++id) {
            auto* item = cJSON_GetArrayItem(json, id);
            assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "label")));
            auto* value = cJSON_GetObjectItemCaseSensitive(item, "id");
            if (effects) {
                const auto& meta = signage_effect_metadata(static_cast<SignageEffect>(id));
                assert(cJSON_IsString(value) && strcmp(value->valuestring, meta.id) == 0);
                assert(cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(item, "supportsPalette")));
                assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "supportsPalette")) == meta.supports_palette);
            } else {
                assert(cJSON_IsNumber(value) && value->valueint == static_cast<int>(id));
            }
        }
        cJSON_Delete(json);
        assert(live_bytes == 0);
        request = {};
        request.authorized = false;
        assert(handler(&request) == ESP_OK && request.body.empty());
        request = {};
        request.fail_at = 1;
        assert(handler(&request) != ESP_OK && "Do not report a disconnected response as complete");
        assert(live_bytes == 0);
        for (int fail_after : {0, 1, 5, 40, 100}) {
            request = {};
            allocations_left = fail_after;
            const int result = handler(&request);
            allocations_left = -1;
            assert(live_bytes == 0);
            cJSON* received = cJSON_Parse(request.body.c_str());
            if (result == ESP_OK && request.status != "500 Internal Server Error") {
                assert(cJSON_IsArray(received));
                assert(cJSON_GetArraySize(received) == static_cast<int>(count));
            }
            cJSON_Delete(received);
            assert(live_bytes == 0);
        }
    }
    cJSON_InitHooks(nullptr);
    printf("PASS: %zu-byte full scene, 8/16 KiB request limits, catalogue HTTP JSON, all effect/palette saves, integer IDs, bounded heap, OOM and disconnects\n", full_scene.size());
}
'''
    with tempfile.TemporaryDirectory(prefix="lumigrid-effect-api-") as temp:
        temp = Path(temp)
        (temp / "driver").mkdir()
        (temp / "driver/gpio.h").write_text("#pragma once\ntypedef enum {" +
            ",".join(f"GPIO_NUM_{n}={n}" for n in range(40)) + "} gpio_num_t;\n")
        (temp / "test.cpp").write_text(harness)
        subprocess.run([cc, "-std=c99", "-c", str(CJSON / "cJSON.c"),
                        "-o", str(temp / "cjson.o")], check=True)
        includes = [temp, CJSON, ROOT / "components/config/include", ROOT / "components/board/include"]
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-sign-compare", "-O1", "-g",
                        *[f"-I{p}" for p in includes], str(temp / "test.cpp"),
                        str(temp / "cjson.o"), "-o", str(temp / "test")], check=True)
        subprocess.run([str(temp / "test")], check=True)


if __name__ == "__main__":
    main()
