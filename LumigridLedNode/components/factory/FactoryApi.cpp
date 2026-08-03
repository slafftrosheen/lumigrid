// components/factory/FactoryApi.cpp
#include "FactoryApi.hpp"
#include "ApiAuth.hpp"

#include "FactoryApp.hpp"
#include "ConfigStore.hpp"
#include "SignageEngine.hpp"
#include "SignageTypes.hpp"
#include "esp_check.h"
#include <inttypes.h>

#include "cJSON.h"
#include "esp_http_server.h"

#include <algorithm>
#include <vector>
#include <memory>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



namespace lg {
namespace {

constexpr size_t kMaxBodyBytes = 512;


esp_err_t send_json(httpd_req_t* request,
                    const char* body,
                    const char* status = "200 OK") {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

esp_err_t send_error(httpd_req_t* request,
                     const char* status,
                     const char* code) {
    char body[128];
    snprintf(body, sizeof(body), R"({"error":"%s"})", code);
    return send_json(request, body, status);
}

esp_err_t read_json(httpd_req_t* request, cJSON** out) {
    if (request->content_len <= 0 ||
        request->content_len > kMaxBodyBytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    char body[kMaxBodyBytes + 1] = {};
    int received = 0;

    while (received < request->content_len) {
        const int result = httpd_req_recv(
            request,
            body + received,
            request->content_len - received
        );

        if (result == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (result <= 0) return ESP_FAIL;

        received += result;
    }

    body[received] = '\0';
    *out = cJSON_ParseWithLength(body, received);

    return *out != nullptr ? ESP_OK : ESP_ERR_INVALID_ARG;
}

bool json_u8(const cJSON* root,
             const char* field,
             uint8_t min,
             uint8_t max,
             uint8_t& target) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, field);

    if (!cJSON_IsNumber(value) ||
        value->valuedouble < min ||
        value->valuedouble > max) {
        return false;
    }

    target = static_cast<uint8_t>(value->valueint);
    return true;
}

bool json_u16(const cJSON* root,
              const char* field,
              uint16_t min,
              uint16_t max,
              uint16_t& target) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, field);

    if (!cJSON_IsNumber(value) ||
        value->valuedouble < min ||
        value->valuedouble > max) {
        return false;
    }

    target = static_cast<uint16_t>(value->valueint);
    return true;
}

bool json_u32(const cJSON* root,
              const char* field,
              uint32_t min,
              uint32_t max,
              uint32_t& target) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, field);

    if (!cJSON_IsNumber(value) ||
        value->valuedouble < min ||
        value->valuedouble > max) {
        return false;
    }

    target = static_cast<uint32_t>(value->valuedouble);
    return true;
}

bool parse_effect(const char* value, FactoryEffect& effect) {
    if (strcmp(value, "identity") == 0) {
        effect = FactoryEffect::Identity;
    } else if (strcmp(value, "pixel_chase") == 0) {
        effect = FactoryEffect::PixelChase;
    } else if (strcmp(value, "rainbow") == 0) {
        effect = FactoryEffect::Rainbow;
    } else if (strcmp(value, "white_walk") == 0) {
        effect = FactoryEffect::WhiteWalk;
    } else if (strcmp(value, "pwm_walk") == 0) {
        effect = FactoryEffect::PwmWalk;
    } else if (strcmp(value, "pwm_fade") == 0) {
        effect = FactoryEffect::PwmFade;
    } else if (strcmp(value, "combined_pulse") == 0) {
        effect = FactoryEffect::CombinedPulse;
    } else if (strcmp(value, "burn_in") == 0) {
        effect = FactoryEffect::BurnIn;
    } else if (strcmp(value, "blackout") == 0) {
        effect = FactoryEffect::SafeBlackout;
    } else {
        return false;
    }

    return true;
}

void build_status_json(const FactoryStatus& status, char* response, size_t max_len) {
    snprintf(
        response,
        max_len,
        "{\"effect\":\"%s\",\"effectStartedMs\":%" PRIu32 
        ",\"durationMs\":%" PRIu32 
        ",\"brightness\":%u,\"autoLoop\":%s,\"outputsEnabled\":%s"
        ",\"testActive\":%s,\"commandRejections\":%" PRIu32 "}",
        effect_name(status.active_effect),
        status.effect_started_ms,
        status.effect_duration_ms,
        status.brightness,
        status.auto_loop ? "true" : "false",
        status.outputs_enabled ? "true" : "false",
        status.test_active ? "true" : "false",
        status.command_rejections
    );
}

esp_err_t get_status(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    const FactoryStatus status = api_ptr->runner().status();

    char response[384];
    build_status_json(status, response, sizeof(response));

    return send_json(request, response);
}

esp_err_t post_effect(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "effect");

    FactoryEffect effect;
    uint8_t brightness = LG_FACTORY_DEFAULT_BRIGHTNESS;
    uint32_t duration_ms = 10'000;

    const bool valid =
        cJSON_IsString(name) &&
        name->valuestring != nullptr &&
        parse_effect(name->valuestring, effect) &&
        (!cJSON_HasObjectItem(root, "brightness") ||
         json_u8(root, "brightness", 0, 96, brightness)) &&
        (!cJSON_HasObjectItem(root, "durationMs") ||
         json_u32(root, "durationMs", 1, LG_FACTORY_MAX_TEST_MS, duration_ms));

    cJSON_Delete(root);

    if (!valid) {
        return send_error(request, "400 Bad Request", "invalid_effect_request");
    }

    FactoryCommand command{
        .type = FactoryCommandType::SelectEffect,
        .effect = effect,
        .brightness = brightness,
        .duration_ms = duration_ms
    };

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().enqueue(command)) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    return send_json(request, R"({"accepted":true})", "202 Accepted");
}

esp_err_t post_lane(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t lane = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t brightness = LG_FACTORY_DEFAULT_BRIGHTNESS;
    uint32_t duration_ms = 10'000;

    const bool valid =
        json_u8(root, "lane", 0, 7, lane) &&
        json_u8(root, "red", 0, 255, red) &&
        json_u8(root, "green", 0, 255, green) &&
        json_u8(root, "blue", 0, 255, blue) &&
        (!cJSON_HasObjectItem(root, "brightness") ||
         json_u8(root, "brightness", 0, 96, brightness)) &&
        (!cJSON_HasObjectItem(root, "durationMs") ||
         json_u32(root, "durationMs", 1, LG_FACTORY_MAX_TEST_MS, duration_ms));

    cJSON_Delete(root);

    if (!valid) {
        return send_error(request, "400 Bad Request", "invalid_lane_request");
    }

    FactoryCommand command{
        .type = FactoryCommandType::PixelLaneSolid,
        .lane_or_channel = lane,
        .red = red,
        .green = green,
        .blue = blue,
        .brightness = brightness,
        .duration_ms = duration_ms
    };

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().enqueue(command)) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    return send_json(request, R"({"accepted":true})", "202 Accepted");
}

esp_err_t post_pwm(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t channel = 0;
    uint16_t duty = 0;
    uint32_t duration_ms = 5000;

    const bool valid =
        json_u8(root, "channel", 0, 7, channel) &&
        json_u16(root, "duty", 0, 4095, duty) &&
        (!cJSON_HasObjectItem(root, "durationMs") ||
         json_u32(root, "durationMs", 1, LG_FACTORY_MAX_TEST_MS, duration_ms));

    cJSON_Delete(root);

    if (!valid) {
        return send_error(request, "400 Bad Request", "invalid_pwm_request");
    }

    FactoryCommand command{
        .type = FactoryCommandType::PwmChannelLevel,
        .lane_or_channel = channel,
        .pwm_duty = duty,
        .duration_ms = duration_ms
    };

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().enqueue(command)) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    return send_json(request, R"({"accepted":true})", "202 Accepted");
}

esp_err_t post_abort(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().enqueue({
            .type = FactoryCommandType::Abort
        })) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    return send_json(request, R"({"accepted":true,"action":"blackout"})");
}

esp_err_t post_auto_loop(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    const bool valid = cJSON_IsBool(enabled);
    const bool value = cJSON_IsTrue(enabled);

    cJSON_Delete(root);

    if (!valid) {
        return send_error(request, "400 Bad Request", "invalid_auto_loop_request");
    }

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().enqueue({
            .type = FactoryCommandType::SetAutoLoop,
            .enabled = value
        })) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    return send_json(request, R"({"accepted":true})", "202 Accepted");
}

esp_err_t ws_handler(httpd_req_t* request) {
    if (request->method == HTTP_GET) {
        return ESP_OK;
    }
    // Read and discard to prevent state leak
    httpd_ws_frame_t ws_pkt = {};
    if (httpd_ws_recv_frame(request, &ws_pkt, 0) == ESP_OK && ws_pkt.len > 0) {
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            httpd_ws_recv_frame(request, &ws_pkt, ws_pkt.len);
            free(buf);
        }
    }
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════
// Signage REST Endpoints
// ═══════════════════════════════════════════════════════════════════

esp_err_t get_presets(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        BrandPreset preset;
        if (api_ptr->config()->load_preset(i, preset) != ESP_OK) continue;

        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "index", i);
        cJSON_AddStringToObject(obj, "name", preset.name);
        cJSON_AddNumberToObject(obj, "brightness", preset.brightness);
        cJSON_AddStringToObject(obj, "effect", signage_effect_name(preset.effect));
        cJSON_AddNumberToObject(obj, "effectSpeedMs", preset.effect_speed_ms);
        cJSON_AddNumberToObject(obj, "transitionMs", preset.transition_ms);

        cJSON* channels = cJSON_CreateArray();
        for (uint8_t ch = 0; ch < 8; ++ch) {
            cJSON* c = cJSON_CreateObject();
            cJSON_AddNumberToObject(c, "r", preset.channels[ch].pixel_color.r);
            cJSON_AddNumberToObject(c, "g", preset.channels[ch].pixel_color.g);
            cJSON_AddNumberToObject(c, "b", preset.channels[ch].pixel_color.b);
            cJSON_AddNumberToObject(c, "pwmDuty", preset.channels[ch].pwm_duty);
            cJSON_AddBoolToObject(c, "pixelEnabled", preset.channels[ch].pixel_enabled);
            cJSON_AddBoolToObject(c, "pwmEnabled", preset.channels[ch].pwm_enabled);
            cJSON_AddItemToArray(channels, c);
        }
        cJSON_AddItemToObject(obj, "channels", channels);
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    esp_err_t ret = send_json(request, json);
    cJSON_free(json);
    return ret;
}

esp_err_t put_preset(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "index", 0, 7, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_index");
    }

    BrandPreset preset{};
    const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (cJSON_IsString(name_j)) preset.set_name(name_j->valuestring);

    json_u8(root, "brightness", 0, 255, preset.brightness);

    const cJSON* effect_j = cJSON_GetObjectItemCaseSensitive(root, "effect");
    if (cJSON_IsString(effect_j)) parse_signage_effect(effect_j->valuestring, preset.effect);

    uint16_t speed = 2000;
    json_u16(root, "effectSpeedMs", 100, 30000, speed);
    preset.effect_speed_ms = speed;

    uint16_t trans = 1000;
    json_u16(root, "transitionMs", 0, 10000, trans);
    preset.transition_ms = trans;

    const cJSON* channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (cJSON_IsArray(channels)) {
        int ch_idx = 0;
        cJSON* ch_item = nullptr;
        cJSON_ArrayForEach(ch_item, channels) {
            if (ch_idx >= 8) break;
            json_u8(ch_item, "r", 0, 255, preset.channels[ch_idx].pixel_color.r);
            json_u8(ch_item, "g", 0, 255, preset.channels[ch_idx].pixel_color.g);
            json_u8(ch_item, "b", 0, 255, preset.channels[ch_idx].pixel_color.b);
            json_u16(ch_item, "pwmDuty", 0, 4095, preset.channels[ch_idx].pwm_duty);

            const cJSON* pe = cJSON_GetObjectItemCaseSensitive(ch_item, "pixelEnabled");
            if (cJSON_IsBool(pe)) preset.channels[ch_idx].pixel_enabled = cJSON_IsTrue(pe);

            const cJSON* pwe = cJSON_GetObjectItemCaseSensitive(ch_item, "pwmEnabled");
            if (cJSON_IsBool(pwe)) preset.channels[ch_idx].pwm_enabled = cJSON_IsTrue(pwe);

            ++ch_idx;
        }
    }

    cJSON_Delete(root);

    esp_err_t err = api_ptr->config()->save_preset(index, preset);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    return send_json(request, R"({"saved":true})", "200 OK");
}

esp_err_t post_activate(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->signage()) return send_error(request, "503 Service Unavailable", "signage_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "preset", 0, 7, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_preset_index");
    }
    cJSON_Delete(root);

    api_ptr->signage()->feed_watchdog();
    api_ptr->signage()->activate_preset(index);

    return send_json(request, R"({"activated":true})", "202 Accepted");
}

esp_err_t get_device_config(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);

    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "deviceName", cfg.device_name);
    cJSON_AddNumberToObject(response, "activePreset", cfg.active_preset_index);
    cJSON_AddNumberToObject(response, "fallbackPreset", cfg.fallback_preset_index);
    cJSON_AddNumberToObject(response, "watchdogTimeoutS", cfg.watchdog_timeout_s);
    cJSON_AddNumberToObject(response, "maxPowerMw", cfg.max_power_mw);
    cJSON_AddNumberToObject(response, "gamma", cfg.gamma_x10 / 10.0f);
    cJSON_AddBoolToObject(response, "softStart", cfg.soft_start_enabled);
    cJSON_AddNumberToObject(response, "softStartMs", cfg.soft_start_ms);
    cJSON_AddStringToObject(response, "timezone", cfg.timezone);
    cJSON_AddBoolToObject(response, "syncEnabled", cfg.sync_enabled);
    cJSON_AddNumberToObject(response, "syncRole", cfg.sync_role);

    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }

    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t put_device_config(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);

    const cJSON* dn = cJSON_GetObjectItemCaseSensitive(root, "deviceName");
    if (cJSON_IsString(dn)) {
        strncpy(cfg.device_name, dn->valuestring, sizeof(cfg.device_name) - 1);
        cfg.device_name[sizeof(cfg.device_name) - 1] = '\0';
    }

    json_u8(root, "activePreset", 0, 7, cfg.active_preset_index);
    json_u8(root, "fallbackPreset", 0, 7, cfg.fallback_preset_index);
    json_u16(root, "watchdogTimeoutS", 0, 3600, cfg.watchdog_timeout_s);
    json_u16(root, "maxPowerMw", 0, 60000, cfg.max_power_mw);
    json_u16(root, "softStartMs", 0, 5000, cfg.soft_start_ms);

    const cJSON* ss = cJSON_GetObjectItemCaseSensitive(root, "softStart");
    if (cJSON_IsBool(ss)) cfg.soft_start_enabled = cJSON_IsTrue(ss);

    const cJSON* tz = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    if (cJSON_IsString(tz)) {
        strncpy(cfg.timezone, tz->valuestring, sizeof(cfg.timezone) - 1);
        cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';
    }

    const cJSON* se = cJSON_GetObjectItemCaseSensitive(root, "syncEnabled");
    if (cJSON_IsBool(se)) cfg.sync_enabled = cJSON_IsTrue(se);

    json_u8(root, "syncRole", 0, 2, cfg.sync_role);

    cJSON_Delete(root);

    if (api_ptr->config()->save_config(cfg) != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }
    return send_json(request, R"({"saved":true})");
}

esp_err_t get_signage_status(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);

    char response[256];
    if (api_ptr->signage()) {
        snprintf(response, sizeof(response),
            "{\"activePreset\":%u,\"fallbackActive\":%s,\"transitioning\":%s}",
            api_ptr->signage()->active_preset_index(),
            api_ptr->signage()->is_fallback_active() ? "true" : "false",
            api_ptr->signage()->is_transitioning() ? "true" : "false"
        );
    } else {
        snprintf(response, sizeof(response), R"({"signage":"not_started"})");
    }

    return send_json(request, response);
}

esp_err_t get_schedules(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < kMaxSchedules; ++i) {
        ScheduleEntry entry;
        if (api_ptr->config()->load_schedule(i, entry) != ESP_OK) continue;

        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "index", i);
        cJSON_AddNumberToObject(obj, "hourStart", entry.hour_start);
        cJSON_AddNumberToObject(obj, "minuteStart", entry.minute_start);
        cJSON_AddNumberToObject(obj, "hourEnd", entry.hour_end);
        cJSON_AddNumberToObject(obj, "minuteEnd", entry.minute_end);
        cJSON_AddNumberToObject(obj, "presetIndex", entry.preset_index);
        cJSON_AddNumberToObject(obj, "daysOfWeek", entry.days_of_week);
        cJSON_AddBoolToObject(obj, "enabled", entry.enabled);
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    esp_err_t ret = send_json(request, json);
    cJSON_free(json);
    return ret;
}

esp_err_t put_schedule(httpd_req_t* request) {
    if (!api::authorize(request)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "index", 0, 7, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_index");
    }

    ScheduleEntry entry{};
    json_u8(root, "hourStart", 0, 23, entry.hour_start);
    json_u8(root, "minuteStart", 0, 59, entry.minute_start);
    json_u8(root, "hourEnd", 0, 23, entry.hour_end);
    json_u8(root, "minuteEnd", 0, 59, entry.minute_end);
    json_u8(root, "presetIndex", 0, 7, entry.preset_index);
    json_u8(root, "daysOfWeek", 0, 127, entry.days_of_week);

    const cJSON* en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (cJSON_IsBool(en)) entry.enabled = cJSON_IsTrue(en);

    cJSON_Delete(root);

    if (api_ptr->config()->save_schedule(index, entry) != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }
    return send_json(request, R"({"saved":true})");
}

} // namespace

#include "dashboard.hpp"

esp_err_t FactoryApi::serve_dashboard(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, dashboard_html, strlen(dashboard_html));
}

FactoryApi::FactoryApi(FactoryRunner& runner) : runner_(runner) {}

esp_err_t FactoryApi::start(uint16_t port, ConfigStore* config, SignageEngine* signage) {
    config_ = config;
    signage_ = signage;

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = port;
    http_config.max_uri_handlers = 20;
    http_config.stack_size = 6144;
    http_config.lru_purge_enable = true;
    http_config.recv_wait_timeout = 5;
    http_config.send_wait_timeout = 5;

    max_open_sockets_ = http_config.max_open_sockets;

    ESP_RETURN_ON_ERROR(httpd_start(&server_, &http_config), "factory-api", "start");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const httpd_uri_t routes[] = {
        // ── Factory test endpoints ──
        { .uri = "/api/v1/factory/status",    .method = HTTP_GET,  .handler = get_status,     .user_ctx = this },
        { .uri = "/api/v1/factory/effect",    .method = HTTP_POST, .handler = post_effect,    .user_ctx = this },
        { .uri = "/api/v1/factory/lane",      .method = HTTP_POST, .handler = post_lane,      .user_ctx = this },
        { .uri = "/api/v1/factory/pwm",       .method = HTTP_POST, .handler = post_pwm,       .user_ctx = this },
        { .uri = "/api/v1/factory/abort",     .method = HTTP_POST, .handler = post_abort,     .user_ctx = this },
        { .uri = "/api/v1/factory/auto-loop", .method = HTTP_POST, .handler = post_auto_loop, .user_ctx = this },
        // ── Signage endpoints ──
        { .uri = "/api/v1/presets",           .method = HTTP_GET,  .handler = get_presets,      .user_ctx = this },
        { .uri = "/api/v1/presets",           .method = HTTP_PUT,  .handler = put_preset,       .user_ctx = this },
        { .uri = "/api/v1/activate",          .method = HTTP_POST, .handler = post_activate,    .user_ctx = this },
        { .uri = "/api/v1/config",            .method = HTTP_GET,  .handler = get_device_config, .user_ctx = this },
        { .uri = "/api/v1/config",            .method = HTTP_PUT,  .handler = put_device_config, .user_ctx = this },
        { .uri = "/api/v1/status",            .method = HTTP_GET,  .handler = get_signage_status, .user_ctx = this },
        { .uri = "/api/v1/schedules",         .method = HTTP_GET,  .handler = get_schedules,    .user_ctx = this },
        { .uri = "/api/v1/schedules",         .method = HTTP_PUT,  .handler = put_schedule,     .user_ctx = this },
        // ── WebSocket + Dashboard ──
        { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = this,
          .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr },
        { .uri = "/",   .method = HTTP_GET, .handler = serve_dashboard, .user_ctx = this },
    };

    for (const auto& endpoint : routes) {
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(server_, &endpoint),
            "api",
            "register uri"
        );
    }

    runner_.set_status_callback([this]() { this->notify_status_async(); });

    return ESP_OK;
}

struct WsBroadcastArg {
    FactoryApi* api;
    char payload[384];
};

void FactoryApi::ws_broadcast_work(void* arg) {
    std::unique_ptr<WsBroadcastArg> msg(static_cast<WsBroadcastArg*>(arg));

    std::vector<int> client_fds(msg->api->max_open_sockets_);
    size_t clients = client_fds.size();

    if (httpd_get_client_list(msg->api->server_, &clients, client_fds.data()) != ESP_OK) {
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(msg->payload);
    frame.len = strlen(msg->payload);

    for (size_t i = 0; i < clients; ++i) {
        if (httpd_ws_get_fd_info(msg->api->server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(msg->api->server_, client_fds[i], &frame);
        }
    }
}

void FactoryApi::notify_status_async() {
    if (server_ == nullptr) return;

    auto* arg = new WsBroadcastArg{};
    arg->api = this;
    build_status_json(runner_.status(), arg->payload, sizeof(arg->payload));

    if (httpd_queue_work(server_, ws_broadcast_work, arg) != ESP_OK) {
        delete arg;
    }
}

void FactoryApi::stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

} // namespace lg
