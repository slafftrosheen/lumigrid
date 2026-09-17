// components/factory/FactoryApi.cpp
#include "FactoryApi.hpp"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "ApiAuth.hpp"
#include "AuditLog.hpp"
#include "DiagnosticLog.hpp"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "FactoryApp.hpp"
#include "ConfigStore.hpp"
#include "LightingRuntime.hpp"
#include "SignageTypes.hpp"
#include "Scheduler.hpp"
#include "NodeSync.hpp"
#include "DmxRouter.hpp"
#include "esp_check.h"
#include <inttypes.h>

#include "cJSON.h"
#include "cjson_helpers.hpp"
#include "esp_http_server.h"

#include <algorithm>
#include <vector>
#include <memory>
#include <new>
#include <limits>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



namespace lg {
namespace {

constexpr size_t kMaxBodyBytes = 8192;


esp_err_t send_json(httpd_req_t* request,
                    const char* body,
                    const char* status = "200 OK") {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-Auth-Token, X-API-Key");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
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

    std::unique_ptr<char[]> body(new (std::nothrow) char[kMaxBodyBytes + 1]());
    if (!body) return ESP_ERR_NO_MEM;

    int received = 0;

    while (received < request->content_len) {
        const int result = httpd_req_recv(
            request,
            body.get() + received,
            request->content_len - received
        );

        if (result == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (result <= 0) return ESP_FAIL;

        received += result;
    }

    body[received] = '\0';
    *out = cJSON_ParseWithLength(body.get(), received);

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

const char* calendar_action_name(CalendarAction action) {
    return action == CalendarAction::Sequence ? "sequence" : "preset";
}

bool parse_calendar_action(const char* name, CalendarAction& action) {
    if (!name) return false;
    if (strcmp(name, "preset") == 0) {
        action = CalendarAction::Preset;
        return true;
    }
    if (strcmp(name, "sequence") == 0) {
        action = CalendarAction::Sequence;
        return true;
    }
    return false;
}

const char* node_role_name(NodeRole role) {
    switch (role) {
        case NodeRole::Auto:   return "auto";
        case NodeRole::Master: return "master";
        case NodeRole::Worker: return "worker";
    }
    return "unknown";
}

void format_ipv4(uint32_t address, char* output, size_t output_size) {
    const uint8_t* octets = reinterpret_cast<const uint8_t*>(&address);
    snprintf(
        output,
        output_size,
        "%u.%u.%u.%u",
        octets[0],
        octets[1],
        octets[2],
        octets[3]
    );
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
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    const FactoryStatus status = api_ptr->runner().status();

    char response[384];
    build_status_json(status, response, sizeof(response));

    return send_json(request, response);
}

esp_err_t post_effect(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

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
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

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
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

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
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().enqueue({
            .type = FactoryCommandType::Abort
        })) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    return send_json(request, R"({"accepted":true,"action":"blackout"})");
}

esp_err_t post_auto_loop(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

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
        // DoS protection: cap frame size to avoid OOM
        if (ws_pkt.len > 1024) {
            return ESP_FAIL;
        }
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            httpd_ws_recv_frame(request, &ws_pkt, ws_pkt.len);
            free(buf);
        }
    }
    return ESP_OK;
}

} // namespace

void RuntimeHealthSnapshot::collect(FactoryApi* api) {
    system.uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    system.free_heap = static_cast<uint32_t>(esp_get_free_heap_size());
    system.min_free_heap = static_cast<uint32_t>(esp_get_minimum_free_heap_size());
    system.reset_reason = static_cast<int>(esp_reset_reason());

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        network.wifi_rssi = ap_info.rssi;
    }

    nvs_stats_t nvs_stats{};
    nvs_get_stats(NULL, &nvs_stats);
    nvs.used_entries = static_cast<int>(nvs_stats.used_entries);
    nvs.free_entries = static_cast<int>(nvs_stats.free_entries);

    if (api->config()) {
        const auto& val = api->config()->cached_validation_result();
        config.valid = val.valid;
        config.error_count = val.error_count;
        config.first_error = val.first_error;
        config.persistence_state = api->config()->persistence_state_str();
        config.primary_valid = api->config()->is_primary_valid();
        config.backup_valid = api->config()->is_backup_valid();
    }

    if (api->diagnostics_provider()) {
        StartupDiagnostics diag = api->diagnostics_provider()();
        startup.state = diag.startup_state;
        startup.stage = diag.startup_stage;
        startup.failing_stage = diag.failing_stage;
        startup.failing_error = diag.failing_error;
    }

    if (api->signage()) {
        auto* sig = api->signage();
        switch (sig->output_state()) {
            case LightingRuntime::OutputState::Healthy: output.state = "Healthy"; break;
            case LightingRuntime::OutputState::Faulted: output.state = "Faulted"; break;
            case LightingRuntime::OutputState::Armed:   output.state = "Armed";   break;
        }
        output.fault = sig->output_fault();
        output.fault_events = sig->fault_event_count();
        output.failed_commits = sig->controller().failed_commit_count();
        output.last_error = static_cast<int>(sig->controller().last_commit_error());
        output.estop_latched = sig->is_output_controller_estop_latched();
        output.runtime_estop = sig->is_estop_active();
        output.control_source = static_cast<int>(sig->current_control_source());
        output.live_objects = sig->active_live_objects();
        output.current_power_mw = sig->power_budget().current_power_mw();
        output.throttling_events = sig->power_budget().throttling_events();
        system.fps = sig->get_fps();
        output.active_preset = sig->active_scene_index();
        output.watchdog_fallback = sig->is_fallback_active();
    } else {
        output.state = "Faulted";
        output.fault = true;
        output.control_source = static_cast<int>(api->runner().control_sources().current_owner());
    }

    if (api->dmx_router()) {
        auto stats = api->dmx_router()->get_stats();
        dmx.sources_active = stats.active_sources;
        dmx.universes_active = stats.active_universes;
        dmx.sources_dropped = stats.sources_dropped;
        dmx.packets_accepted = stats.packets_accepted;
        dmx.packets_rejected_seq = stats.packets_rejected_sequence;
        dmx.packets_ignored_unpatched = stats.packets_ignored_unpatched;
        dmx.submission_failures = stats.submission_failures;
        dmx.universe_count = api->dmx_router()->get_active_universes(
            dmx.configured_universes, kMaxUniverses);
    }

    if (api->node_sync()) {
        sync.packet_loss = api->node_sync()->status().packets_missed;
    }
}

cJSON* RuntimeHealthSnapshot::to_json() const {
    cJSON* root = cJSON_CreateObject();
    if (!root) return nullptr;

    cJSON_AddNumberToObject(root, "schema_version", kSchemaVersion);

    cJSON* sys = cJSON_AddObjectToObject(root, "system");
    cJSON_AddNumberToObject(sys, "uptime_s", system.uptime_s);
    cJSON_AddNumberToObject(sys, "free_heap", system.free_heap);
    cJSON_AddNumberToObject(sys, "min_free_heap", system.min_free_heap);
    cJSON_AddNumberToObject(sys, "reset_reason", system.reset_reason);
    cJSON_AddNumberToObject(sys, "fps", system.fps);

    cJSON* net = cJSON_AddObjectToObject(root, "network");
    cJSON_AddNumberToObject(net, "wifi_rssi", network.wifi_rssi);

    cJSON* nvs_obj = cJSON_AddObjectToObject(root, "nvs");
    cJSON_AddNumberToObject(nvs_obj, "used", nvs.used_entries);
    cJSON_AddNumberToObject(nvs_obj, "free", nvs.free_entries);

    cJSON* cfg = cJSON_AddObjectToObject(root, "config");
    cJSON_AddBoolToObject(cfg, "valid", config.valid);
    cJSON_AddNumberToObject(cfg, "error_count", config.error_count);
    cJSON_AddStringToObject(cfg, "first_error", config.first_error);
    cJSON_AddStringToObject(cfg, "persistence_state", config.persistence_state);
    cJSON_AddBoolToObject(cfg, "primary_valid", config.primary_valid);
    cJSON_AddBoolToObject(cfg, "backup_valid", config.backup_valid);

    cJSON* su = cJSON_AddObjectToObject(root, "startup");
    cJSON_AddStringToObject(su, "state", startup.state);
    cJSON_AddStringToObject(su, "stage", startup.stage);
    cJSON_AddStringToObject(su, "failing_stage", startup.failing_stage);
    cJSON_AddNumberToObject(su, "failing_error", startup.failing_error);

    cJSON* out = cJSON_AddObjectToObject(root, "output");
    cJSON_AddStringToObject(out, "state", output.state);
    cJSON_AddBoolToObject(out, "fault", output.fault);
    cJSON_AddNumberToObject(out, "fault_events", output.fault_events);
    cJSON_AddNumberToObject(out, "failed_commits", output.failed_commits);
    cJSON_AddNumberToObject(out, "last_error", output.last_error);
    cJSON_AddBoolToObject(out, "estop_latched", output.estop_latched);
    cJSON_AddBoolToObject(out, "runtime_estop", output.runtime_estop);
    cJSON_AddNumberToObject(out, "control_source", output.control_source);
    cJSON_AddNumberToObject(out, "live_objects", output.live_objects);
    cJSON_AddNumberToObject(out, "current_power_mw", output.current_power_mw);
    cJSON_AddNumberToObject(out, "throttling_events", output.throttling_events);
    cJSON_AddNumberToObject(out, "active_preset", output.active_preset);
    cJSON_AddBoolToObject(out, "watchdog_fallback", output.watchdog_fallback);

    cJSON* dmx_obj = cJSON_AddObjectToObject(root, "dmx");
    cJSON_AddNumberToObject(dmx_obj, "sources_active", dmx.sources_active);
    cJSON_AddNumberToObject(dmx_obj, "universes_active", dmx.universes_active);
    cJSON_AddNumberToObject(dmx_obj, "sources_dropped", dmx.sources_dropped);
    cJSON_AddNumberToObject(dmx_obj, "packets_accepted", dmx.packets_accepted);
    cJSON_AddNumberToObject(dmx_obj, "packets_rejected_seq", dmx.packets_rejected_seq);
    cJSON_AddNumberToObject(dmx_obj, "packets_ignored_unpatched", dmx.packets_ignored_unpatched);
    cJSON_AddNumberToObject(dmx_obj, "submission_failures", dmx.submission_failures);
    cJSON* univ_arr = cJSON_CreateArray();
    for (size_t i = 0; i < dmx.universe_count; ++i) {
        cJSON_AddItemToArray(univ_arr, cJSON_CreateNumber(dmx.configured_universes[i]));
    }
    cJSON_AddItemToObject(dmx_obj, "configured_universes", univ_arr);

    cJSON* sync_obj = cJSON_AddObjectToObject(root, "sync");
    cJSON_AddNumberToObject(sync_obj, "packet_loss", sync.packet_loss);

    return root;
}

esp_err_t get_health(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    auto* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    RuntimeHealthSnapshot snapshot;
    snapshot.collect(api_ptr);

    cJSON* root = snapshot.to_json();
    if (!root) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    esp_err_t ret = send_json(request, json);
    cJSON_free(json);
    return ret;
}

esp_err_t get_logs(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    char* logs = new (std::nothrow) char[DiagnosticLog::kBufferSize + 1];
    if (!logs) return send_error(request, "500 Internal Server Error", "Out of Memory");
    
    DiagnosticLog::instance().copy_logs(logs, DiagnosticLog::kBufferSize + 1);
    
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_set_type(request, "text/plain");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, logs);
    
    delete[] logs;
    return err;
}

esp_err_t post_auth_update(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);

    const cJSON* admin = cJSON_GetObjectItemCaseSensitive(root, "adminToken");
    const cJSON* installer = cJSON_GetObjectItemCaseSensitive(root, "installerToken");
    const cJSON* op = cJSON_GetObjectItemCaseSensitive(root, "operatorToken");
    const cJSON* new_tok = cJSON_GetObjectItemCaseSensitive(root, "newToken");

    auto validate_and_copy = [](const cJSON* item, char* dest, size_t max_len) {
        if (cJSON_IsString(item) && strlen(item->valuestring) >= 4 && strlen(item->valuestring) < max_len) {
            strncpy(dest, item->valuestring, max_len - 1);
            dest[max_len - 1] = '\0';
            return true;
        }
        return false;
    };

    bool updated = false;
    if (new_tok && validate_and_copy(new_tok, cfg.admin_token, sizeof(cfg.admin_token))) updated = true;
    if (admin && validate_and_copy(admin, cfg.admin_token, sizeof(cfg.admin_token))) updated = true;
    if (installer && validate_and_copy(installer, cfg.installer_token, sizeof(cfg.installer_token))) updated = true;
    if (op && validate_and_copy(op, cfg.operator_token, sizeof(cfg.operator_token))) updated = true;

    if (!updated) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_token_format");
    }

    if (api_ptr->config()->save_config(cfg) == ESP_OK) {
        api::update_tokens(cfg.admin_token, cfg.installer_token, cfg.operator_token);
        api::AuditLog::instance().log(api::ApiRole::Admin, "auth_update", "Tokens updated");
        cJSON_Delete(root);
        return send_json(request, R"({"updated":true})", "200 OK");
    }

    cJSON_Delete(root);
    return send_error(request, "500 Internal Server Error", "nvs_write_failed");
}

// ═══════════════════════════════════════════════════════════════════
// Signage REST Endpoints
// ═══════════════════════════════════════════════════════════════════

esp_err_t get_presets(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_send_chunk(request, "[", 1);

    bool first = true;
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneConfig scene;
        if (api_ptr->config()->load_scene(i, scene) != ESP_OK) continue;

        cJSON* obj = cJSON_CreateObject();
        if (!obj) continue;
        cJSON_AddNumberToObject(obj, "index", i);
        cJSON_AddStringToObject(obj, "name", scene.name);
        cJSON_AddNumberToObject(obj, "globalBrightness", scene.global_brightness);
        cJSON_AddNumberToObject(obj, "transitionMs", scene.transition_ms);

        cJSON* zones = cJSON_CreateArray();
        for (uint8_t z = 0; z < 8; ++z) {
            cJSON* zone_obj = cJSON_CreateObject();
            cJSON* layers = cJSON_CreateArray();
            for (uint8_t l = 0; l < 3; ++l) {
                cJSON* layer = cJSON_CreateObject();
                cJSON_AddStringToObject(layer, "effect", signage_effect_name(static_cast<SignageEffect>(scene.zones[z].layers[l].effect)));
                cJSON_AddStringToObject(layer, "blendMode", blend_mode_name(scene.zones[z].layers[l].blend_mode));
                
                cJSON* pcolor = cJSON_CreateObject();
                cJSON_AddNumberToObject(pcolor, "r", scene.zones[z].layers[l].primary_color.r);
                cJSON_AddNumberToObject(pcolor, "g", scene.zones[z].layers[l].primary_color.g);
                cJSON_AddNumberToObject(pcolor, "b", scene.zones[z].layers[l].primary_color.b);
                cJSON_AddItemToObject(layer, "primaryColor", pcolor);
                
                cJSON* scolor = cJSON_CreateObject();
                cJSON_AddNumberToObject(scolor, "r", scene.zones[z].layers[l].secondary_color.r);
                cJSON_AddNumberToObject(scolor, "g", scene.zones[z].layers[l].secondary_color.g);
                cJSON_AddNumberToObject(scolor, "b", scene.zones[z].layers[l].secondary_color.b);
                cJSON_AddItemToObject(layer, "secondaryColor", scolor);

                cJSON_AddNumberToObject(layer, "speedMs", scene.zones[z].layers[l].speed_ms);
                cJSON_AddNumberToObject(layer, "fxParam", scene.zones[z].layers[l].fx_param);
                cJSON_AddNumberToObject(layer, "paletteId", scene.zones[z].layers[l].palette_id);
                cJSON_AddNumberToObject(layer, "opacity", scene.zones[z].layers[l].opacity);
                cJSON_AddBoolToObject(layer, "enabled", scene.zones[z].layers[l].enabled);
                cJSON_AddItemToArray(layers, layer);
            }
            cJSON_AddItemToObject(zone_obj, "layers", layers);
            cJSON_AddItemToArray(zones, zone_obj);
        }
        cJSON_AddItemToObject(obj, "zones", zones);

        char* json = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (json) {
            if (!first) {
                httpd_resp_send_chunk(request, ",", 1);
            }
            first = false;
            httpd_resp_send_chunk(request, json, strlen(json));
            cJSON_free(json);
        }
    }

    httpd_resp_send_chunk(request, "]", 1);
    httpd_resp_send_chunk(request, nullptr, 0);
    return ESP_OK;
}

esp_err_t put_preset(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "index", 0, kMaxPresets - 1, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_index");
    }

#define P_VALIDATE_U8(obj, field, min, max, target)     do {         const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, field);         if (v) {             if (!cJSON_IsNumber(v) || v->valuedouble < (min) || v->valuedouble > (max)) {                 cJSON_Delete(root);                 return send_error(request, "400 Bad Request", "invalid_field_" field);             }             target = static_cast<uint8_t>(v->valueint);         }     } while(0)

#define P_VALIDATE_U16(obj, field, min, max, target)     do {         const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, field);         if (v) {             if (!cJSON_IsNumber(v) || v->valuedouble < (min) || v->valuedouble > (max)) {                 cJSON_Delete(root);                 return send_error(request, "400 Bad Request", "invalid_field_" field);             }             target = static_cast<uint16_t>(v->valueint);         }     } while(0)

    SceneConfig scene{};
    const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (name_j) {
        if (!cJSON_IsString(name_j) || strlen(name_j->valuestring) >= sizeof(scene.name)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_name");
        }
        strncpy(scene.name, name_j->valuestring, sizeof(scene.name) - 1);
        scene.name[sizeof(scene.name) - 1] = '\0';
    }

    P_VALIDATE_U8(root, "globalBrightness", 0, 255, scene.global_brightness);
    P_VALIDATE_U16(root, "transitionMs", 0, 10000, scene.transition_ms);

    const cJSON* zones = cJSON_GetObjectItemCaseSensitive(root, "zones");
    if (zones) {
        if (!cJSON_IsArray(zones) || cJSON_GetArraySize(zones) > 8) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_zones");
        }
        int z_idx = 0;
        cJSON* z_item = nullptr;
        cJSON_ArrayForEach(z_item, zones) {
            if (z_idx >= 8) break;
            
            const cJSON* layers = cJSON_GetObjectItemCaseSensitive(z_item, "layers");
            if (layers) {
                if (!cJSON_IsArray(layers) || cJSON_GetArraySize(layers) > 3) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "invalid_field_layers");
                }
                int l_idx = 0;
                cJSON* l_item = nullptr;
                cJSON_ArrayForEach(l_item, layers) {
                    if (l_idx >= 3) break;
                    
                    const cJSON* effect_j = cJSON_GetObjectItemCaseSensitive(l_item, "effect");
                    if (effect_j) {
                        SignageEffect eff = SignageEffect::Solid;
                        if (!cJSON_IsString(effect_j) || !parse_signage_effect(effect_j->valuestring, eff)) {
                            cJSON_Delete(root);
                            return send_error(request, "400 Bad Request", "invalid_field_effect");
                        }
                        scene.zones[z_idx].layers[l_idx].effect = static_cast<uint8_t>(eff);
                    }
                    
                    const cJSON* blend_j = cJSON_GetObjectItemCaseSensitive(l_item, "blendMode");
                    if (blend_j) {
                        if (!cJSON_IsString(blend_j) || !parse_blend_mode(blend_j->valuestring, scene.zones[z_idx].layers[l_idx].blend_mode)) {
                            cJSON_Delete(root);
                            return send_error(request, "400 Bad Request", "invalid_field_blendMode");
                        }
                    }
                    
                    const cJSON* pc = cJSON_GetObjectItemCaseSensitive(l_item, "primaryColor");
                    if (pc) {
                        P_VALIDATE_U8(pc, "r", 0, 255, scene.zones[z_idx].layers[l_idx].primary_color.r);
                        P_VALIDATE_U8(pc, "g", 0, 255, scene.zones[z_idx].layers[l_idx].primary_color.g);
                        P_VALIDATE_U8(pc, "b", 0, 255, scene.zones[z_idx].layers[l_idx].primary_color.b);
                    }
                    
                    const cJSON* sc = cJSON_GetObjectItemCaseSensitive(l_item, "secondaryColor");
                    if (sc) {
                        P_VALIDATE_U8(sc, "r", 0, 255, scene.zones[z_idx].layers[l_idx].secondary_color.r);
                        P_VALIDATE_U8(sc, "g", 0, 255, scene.zones[z_idx].layers[l_idx].secondary_color.g);
                        P_VALIDATE_U8(sc, "b", 0, 255, scene.zones[z_idx].layers[l_idx].secondary_color.b);
                    }
                    
                    P_VALIDATE_U16(l_item, "speedMs", 10, 60000, scene.zones[z_idx].layers[l_idx].speed_ms);
                    P_VALIDATE_U8(l_item, "fxParam", 0, 255, scene.zones[z_idx].layers[l_idx].fx_param);
                    P_VALIDATE_U8(l_item, "paletteId", 0, 255, scene.zones[z_idx].layers[l_idx].palette_id);
                    P_VALIDATE_U8(l_item, "opacity", 0, 255, scene.zones[z_idx].layers[l_idx].opacity);
                    
                    const cJSON* en = cJSON_GetObjectItemCaseSensitive(l_item, "enabled");
                    if (en) {
                        if (!cJSON_IsBool(en)) { cJSON_Delete(root); return send_error(request, "400 Bad Request", "invalid_field_enabled"); }
                        scene.zones[z_idx].layers[l_idx].enabled = cJSON_IsTrue(en);
                    }
                    
                    l_idx++;
                }
            }
            z_idx++;
        }
    }
#undef P_VALIDATE_U8
#undef P_VALIDATE_U16

    cJSON_Delete(root);

    esp_err_t err = api_ptr->config()->save_scene(index, scene);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    char detail[64];
    snprintf(detail, sizeof(detail), "Preset %d updated", index);
    api::AuditLog::instance().log(api::ApiRole::Operator, "preset_update", detail);

    return send_json(request, R"({"saved":true})", "200 OK");
}

esp_err_t post_activate(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->signage()) return send_error(request, "503 Service Unavailable", "signage_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "preset", 0, kMaxPresets - 1, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_preset_index");
    }

    api_ptr->signage()->feed_watchdog();
    api_ptr->signage()->release_control(ControlSource::EStop);
    api_ptr->signage()->acquire_control(ControlSource::ManualOverride, 0);
    cJSON_Delete(root);
    api_ptr->signage()->activate_scene(index);

    char resp[64];
    snprintf(resp, sizeof(resp), R"({"activated":true,"preset":%u})", index);
    return send_json(request, resp, "202 Accepted");
}

esp_err_t get_device_config(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);

    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "deviceName", cfg.device_name);
    cJSON_AddStringToObject(response, "serialNumber", cfg.serial_number);
    cJSON_AddStringToObject(response, "hardwareRevision", cfg.hardware_revision);
    cJSON_AddStringToObject(response, "siteLabel", cfg.site_label);
    cJSON_AddStringToObject(response, "groupLabel", cfg.group_label);
    
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
    cJSON_AddBoolToObject(response, "mqttEnabled", cfg.mqtt_enabled);
    cJSON_AddStringToObject(response, "mqttBroker", cfg.mqtt_broker);
    cJSON_AddNumberToObject(response, "mqttPort", cfg.mqtt_port);

    cJSON* zones = cJSON_CreateArray();
    for (uint8_t i = 0; i < 8; ++i) {
        cJSON* z = cJSON_CreateObject();
        cJSON_AddStringToObject(z, "name", cfg.zones[i].name);
        cJSON_AddStringToObject(z, "role", zone_role_name(cfg.zones[i].role));
        cJSON_AddItemToArray(zones, z);
    }
    cJSON_AddItemToObject(response, "zones", zones);

    cJSON* phys = cJSON_CreateArray();
    for (uint8_t i = 0; i < 16; ++i) {
        cJSON* p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "type", static_cast<int>(cfg.physical_outputs[i].type));
        cJSON_AddNumberToObject(p, "hardwareId", cfg.physical_outputs[i].hardware_id);
        cJSON_AddNumberToObject(p, "gpioPin", cfg.physical_outputs[i].gpio_pin);
        cJSON_AddBoolToObject(p, "enabled", cfg.physical_outputs[i].enabled);
        cJSON_AddItemToArray(phys, p);
    }
    cJSON_AddItemToObject(response, "physicalOutputs", phys);

    cJSON* fixt = cJSON_CreateArray();
    for (uint8_t i = 0; i < 16; ++i) {
        cJSON* f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "profile", static_cast<int>(cfg.fixtures[i].profile));
        cJSON_AddNumberToObject(f, "pixelOrder", static_cast<int>(cfg.fixtures[i].pixel_order));
        cJSON* p_idx = cJSON_CreateArray();
        for (uint8_t j = 0; j < 4; ++j) cJSON_AddItemToArray(p_idx, cJSON_CreateNumber(cfg.fixtures[i].physical_output_index[j]));
        cJSON_AddItemToObject(f, "physicalOutputIndex", p_idx);
        cJSON_AddNumberToObject(f, "numPixels", cfg.fixtures[i].num_pixels);
        cJSON_AddNumberToObject(f, "maxBrightness", cfg.fixtures[i].max_brightness);
        cJSON* wb = cJSON_CreateArray();
        for (uint8_t j = 0; j < 3; ++j) cJSON_AddItemToArray(wb, cJSON_CreateNumber(cfg.fixtures[i].white_balance[j]));
        cJSON_AddItemToObject(f, "whiteBalance", wb);
        cJSON_AddBoolToObject(f, "gammaCorrection", cfg.fixtures[i].gamma_correction);
        cJSON_AddNumberToObject(f, "maxMilliwatts", cfg.fixtures[i].max_milliwatts);
        cJSON_AddNumberToObject(f, "zoneId", cfg.fixtures[i].zone_id);
        cJSON_AddNumberToObject(f, "dmxUniverse", cfg.fixtures[i].dmx_universe);
        cJSON_AddNumberToObject(f, "dmxStartChannel", cfg.fixtures[i].dmx_start_channel);
        cJSON_AddNumberToObject(f, "dmxMode", cfg.fixtures[i].dmx_mode);
        cJSON_AddItemToArray(fixt, f);
    }
    cJSON_AddItemToObject(response, "fixtures", fixt);

    cJSON* prot_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(prot_json, "artnetEnabled", cfg.protocol.artnet_enabled);
    cJSON_AddBoolToObject(prot_json, "sacnEnabled", cfg.protocol.sacn_enabled);
    cJSON_AddNumberToObject(prot_json, "universeStart", cfg.protocol.universe_start);
    cJSON_AddStringToObject(prot_json, "mergeMode", cfg.protocol.merge_mode == MergeMode::HTP ? "HTP" : "LTP");
    cJSON_AddNumberToObject(prot_json, "streamTimeoutMs", cfg.protocol.stream_timeout_ms);
    cJSON_AddNumberToObject(prot_json, "liveStreamLayer", cfg.protocol.live_stream_layer);
    cJSON_AddNumberToObject(prot_json, "zoneStartChannel", cfg.protocol.zone_start_channel);
    cJSON_AddItemToObject(response, "protocol", prot_json);

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
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

#define VALIDATE_U8(field, min, max, target) \
    do { \
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, field); \
        if (v) { \
            if (!cJSON_IsNumber(v) || v->valuedouble < (min) || v->valuedouble > (max)) { \
                cJSON_Delete(root); \
                return send_error(request, "400 Bad Request", "invalid_field_" field); \
            } \
            target = static_cast<uint8_t>(v->valueint); \
        } \
    } while(0)

#define VALIDATE_U16(field, min, max, target) \
    do { \
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, field); \
        if (v) { \
            if (!cJSON_IsNumber(v) || v->valuedouble < (min) || v->valuedouble > (max)) { \
                cJSON_Delete(root); \
                return send_error(request, "400 Bad Request", "invalid_field_" field); \
            } \
            target = static_cast<uint16_t>(v->valueint); \
        } \
    } while(0)

#define VALIDATE_STRING(field, max_len, target) \
    do { \
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, field); \
        if (v) { \
            if (!cJSON_IsString(v) || strlen(v->valuestring) >= (max_len)) { \
                cJSON_Delete(root); \
                return send_error(request, "400 Bad Request", "invalid_field_" field); \
            } \
            strncpy(target, v->valuestring, (max_len) - 1); \
            target[(max_len) - 1] = '\0'; \
        } \
    } while(0)

    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);

    VALIDATE_STRING("deviceName", sizeof(cfg.device_name), cfg.device_name);
    VALIDATE_STRING("serialNumber", sizeof(cfg.serial_number), cfg.serial_number);
    VALIDATE_STRING("hardwareRevision", sizeof(cfg.hardware_revision), cfg.hardware_revision);
    VALIDATE_STRING("siteLabel", sizeof(cfg.site_label), cfg.site_label);
    VALIDATE_STRING("groupLabel", sizeof(cfg.group_label), cfg.group_label);

    VALIDATE_U8("activePreset", 0, kMaxPresets - 1, cfg.active_preset_index);
    VALIDATE_U8("fallbackPreset", 0, kMaxPresets - 1, cfg.fallback_preset_index);
    VALIDATE_U16("watchdogTimeoutS", 0, 3600, cfg.watchdog_timeout_s);
    VALIDATE_U16("maxPowerMw", 0, 60000, cfg.max_power_mw);
    VALIDATE_U16("softStartMs", 0, 5000, cfg.soft_start_ms);

    const cJSON* ss = cJSON_GetObjectItemCaseSensitive(root, "softStart");
    if (ss) {
        if (!cJSON_IsBool(ss)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_softStart");
        }
        cfg.soft_start_enabled = cJSON_IsTrue(ss);
    }

    VALIDATE_STRING("timezone", sizeof(cfg.timezone), cfg.timezone);

    const cJSON* se = cJSON_GetObjectItemCaseSensitive(root, "syncEnabled");
    if (se) {
        if (!cJSON_IsBool(se)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_syncEnabled");
        }
        cfg.sync_enabled = cJSON_IsTrue(se);
    }

    const cJSON* me = cJSON_GetObjectItemCaseSensitive(root, "mqttEnabled");
    if (me) {
        if (!cJSON_IsBool(me)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_mqttEnabled");
        }
        cfg.mqtt_enabled = cJSON_IsTrue(me);
    }

    VALIDATE_STRING("mqttBroker", sizeof(cfg.mqtt_broker), cfg.mqtt_broker);
    VALIDATE_U16("mqttPort", 1, 65535, cfg.mqtt_port);

    VALIDATE_U8("syncRole", 0, 2, cfg.sync_role);

    const cJSON* zones_arr = cJSON_GetObjectItemCaseSensitive(root, "zones");
    if (zones_arr) {
        if (!cJSON_IsArray(zones_arr) || cJSON_GetArraySize(zones_arr) > 8) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_zones");
        }
        int z_idx = 0;
        cJSON* z_item = nullptr;
        cJSON_ArrayForEach(z_item, zones_arr) {
            if (z_idx >= 8) break;
            const cJSON* z_name = cJSON_GetObjectItemCaseSensitive(z_item, "name");
            if (z_name) {
                if (!cJSON_IsString(z_name) || strlen(z_name->valuestring) >= sizeof(cfg.zones[z_idx].name)) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "invalid_field_zones_name");
                }
                strncpy(cfg.zones[z_idx].name, z_name->valuestring, sizeof(cfg.zones[z_idx].name) - 1);
                cfg.zones[z_idx].name[sizeof(cfg.zones[z_idx].name) - 1] = '\0';
            }
            const cJSON* z_role = cJSON_GetObjectItemCaseSensitive(z_item, "role");
            if (z_role) {
                if (!cJSON_IsString(z_role) || !parse_zone_role(z_role->valuestring, cfg.zones[z_idx].role)) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "invalid_field_zones_role");
                }
            }
            ++z_idx;
        }
    }

    const cJSON* phys_arr = cJSON_GetObjectItemCaseSensitive(root, "physicalOutputs");
    if (phys_arr && cJSON_IsArray(phys_arr)) {
        int p_idx = 0;
        cJSON* p_item = nullptr;
        cJSON_ArrayForEach(p_item, phys_arr) {
            if (p_idx >= 16) break;
            const cJSON* p_type = cJSON_GetObjectItemCaseSensitive(p_item, "type");
            if (p_type && cJSON_IsNumber(p_type)) cfg.physical_outputs[p_idx].type = static_cast<OutputType>(p_type->valueint);
            
            const cJSON* p_hwid = cJSON_GetObjectItemCaseSensitive(p_item, "hardwareId");
            if (p_hwid && cJSON_IsNumber(p_hwid)) cfg.physical_outputs[p_idx].hardware_id = static_cast<uint8_t>(p_hwid->valueint);
            
            const cJSON* p_pin = cJSON_GetObjectItemCaseSensitive(p_item, "gpioPin");
            if (p_pin && cJSON_IsNumber(p_pin)) cfg.physical_outputs[p_idx].gpio_pin = static_cast<uint8_t>(p_pin->valueint);
            
            const cJSON* p_en = cJSON_GetObjectItemCaseSensitive(p_item, "enabled");
            if (p_en && cJSON_IsBool(p_en)) cfg.physical_outputs[p_idx].enabled = cJSON_IsTrue(p_en);
            
            ++p_idx;
        }
    }

    const cJSON* fixt_arr = cJSON_GetObjectItemCaseSensitive(root, "fixtures");
    if (fixt_arr && cJSON_IsArray(fixt_arr)) {
        int f_idx = 0;
        cJSON* f_item = nullptr;
        cJSON_ArrayForEach(f_item, fixt_arr) {
            if (f_idx >= 16) break;
            const cJSON* f_prof = cJSON_GetObjectItemCaseSensitive(f_item, "profile");
            if (f_prof && cJSON_IsNumber(f_prof)) cfg.fixtures[f_idx].profile = static_cast<FixtureProfile>(f_prof->valueint);
            
            const cJSON* f_po = cJSON_GetObjectItemCaseSensitive(f_item, "pixelOrder");
            if (f_po && cJSON_IsNumber(f_po)) cfg.fixtures[f_idx].pixel_order = static_cast<PixelOrder>(f_po->valueint);
            
            const cJSON* f_idx_arr = cJSON_GetObjectItemCaseSensitive(f_item, "physicalOutputIndex");
            if (f_idx_arr && cJSON_IsArray(f_idx_arr)) {
                int j = 0;
                cJSON* i_item = nullptr;
                cJSON_ArrayForEach(i_item, f_idx_arr) {
                    if (j >= 4) break;
                    if (cJSON_IsNumber(i_item)) cfg.fixtures[f_idx].physical_output_index[j] = static_cast<uint8_t>(i_item->valueint);
                    ++j;
                }
            }
            
            const cJSON* f_np = cJSON_GetObjectItemCaseSensitive(f_item, "numPixels");
            if (f_np && cJSON_IsNumber(f_np)) {
                if (f_np->valueint < 0 || f_np->valueint > MAX_FIXTURE_PIXELS) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "fixture_pixel_limit_exceeded_max_256");
                }
                cfg.fixtures[f_idx].num_pixels = static_cast<uint16_t>(f_np->valueint);
            }
            
            const cJSON* f_mb = cJSON_GetObjectItemCaseSensitive(f_item, "maxBrightness");
            if (f_mb && cJSON_IsNumber(f_mb)) cfg.fixtures[f_idx].max_brightness = static_cast<uint8_t>(f_mb->valueint);
            
            const cJSON* f_wb = cJSON_GetObjectItemCaseSensitive(f_item, "whiteBalance");
            if (f_wb && cJSON_IsArray(f_wb)) {
                int j = 0;
                cJSON* w_item = nullptr;
                cJSON_ArrayForEach(w_item, f_wb) {
                    if (j >= 3) break;
                    if (cJSON_IsNumber(w_item)) cfg.fixtures[f_idx].white_balance[j] = static_cast<uint8_t>(w_item->valueint);
                    ++j;
                }
            }
            
            const cJSON* f_gc = cJSON_GetObjectItemCaseSensitive(f_item, "gammaCorrection");
            if (f_gc && cJSON_IsBool(f_gc)) cfg.fixtures[f_idx].gamma_correction = cJSON_IsTrue(f_gc);
            
            const cJSON* f_mm = cJSON_GetObjectItemCaseSensitive(f_item, "maxMilliwatts");
            if (f_mm && cJSON_IsNumber(f_mm)) cfg.fixtures[f_idx].max_milliwatts = static_cast<uint16_t>(f_mm->valueint);
            
            const cJSON* f_zi = cJSON_GetObjectItemCaseSensitive(f_item, "zoneId");
            if (f_zi && cJSON_IsNumber(f_zi)) cfg.fixtures[f_idx].zone_id = static_cast<uint8_t>(f_zi->valueint);

            const cJSON* f_du = cJSON_GetObjectItemCaseSensitive(f_item, "dmxUniverse");
            if (f_du && cJSON_IsNumber(f_du)) cfg.fixtures[f_idx].dmx_universe = static_cast<uint16_t>(f_du->valueint);

            const cJSON* f_dc = cJSON_GetObjectItemCaseSensitive(f_item, "dmxStartChannel");
            if (f_dc && cJSON_IsNumber(f_dc)) cfg.fixtures[f_idx].dmx_start_channel = static_cast<uint16_t>(f_dc->valueint);

            const cJSON* f_dm = cJSON_GetObjectItemCaseSensitive(f_item, "dmxMode");
            if (f_dm && cJSON_IsNumber(f_dm)) cfg.fixtures[f_idx].dmx_mode = static_cast<uint8_t>(f_dm->valueint);
            
            ++f_idx;
        }
    }

    const cJSON* prot = cJSON_GetObjectItemCaseSensitive(root, "protocol");
    if (prot && cJSON_IsObject(prot)) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(prot, "artnetEnabled");
        if (item && cJSON_IsBool(item)) cfg.protocol.artnet_enabled = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(prot, "sacnEnabled");
        if (item && cJSON_IsBool(item)) cfg.protocol.sacn_enabled = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(prot, "universeStart");
        if (item && cJSON_IsNumber(item)) cfg.protocol.universe_start = static_cast<uint16_t>(item->valueint);

        item = cJSON_GetObjectItemCaseSensitive(prot, "mergeMode");
        if (item && cJSON_IsString(item)) {
            if (strcmp(item->valuestring, "LTP") == 0) cfg.protocol.merge_mode = MergeMode::LTP;
            else cfg.protocol.merge_mode = MergeMode::HTP;
        }

        item = cJSON_GetObjectItemCaseSensitive(prot, "streamTimeoutMs");
        if (item && cJSON_IsNumber(item)) cfg.protocol.stream_timeout_ms = static_cast<uint16_t>(item->valueint);

        item = cJSON_GetObjectItemCaseSensitive(prot, "liveStreamLayer");
        if (item && cJSON_IsNumber(item)) cfg.protocol.live_stream_layer = static_cast<uint8_t>(item->valueint);

        item = cJSON_GetObjectItemCaseSensitive(prot, "zoneStartChannel");
        if (item && cJSON_IsNumber(item)) cfg.protocol.zone_start_channel = static_cast<uint16_t>(item->valueint);
    }
#undef VALIDATE_U8
#undef VALIDATE_U16
#undef VALIDATE_STRING

    cJSON_Delete(root);

    if (api_ptr->config()->save_config(cfg) != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    if (api_ptr->signage()) {
        api_ptr->signage()->reload_device_config();
    }
    
    api::AuditLog::instance().log(api::ApiRole::Admin, "device_config", "Config updated");
    return send_json(request, R"({"saved":true})");
}

esp_err_t get_signage_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);

    cJSON* response = cJSON_CreateObject();
    if (api_ptr->signage()) {
        cJSON_AddNumberToObject(
            response,
            "activePreset",
            api_ptr->signage()->active_scene_index()
        );
        cJSON_AddBoolToObject(
            response,
            "fallbackActive",
            api_ptr->signage()->is_fallback_active()
        );
        cJSON_AddBoolToObject(
            response,
            "transitioning",
            api_ptr->signage()->is_transitioning()
        );
        cJSON_AddNumberToObject(
            response,
            "estimatedMilliwatts",
            api_ptr->signage()->power_budget().current_power_mw()
        );
        cJSON_AddBoolToObject(
            response,
            "outputsEnabled",
            api_ptr->signage()->controller().output_enabled()
        );
        
        const char* owner = "Unknown";
        switch (api_ptr->signage()->current_control_source()) {
            case ControlSource::ScenePlayback: owner = "ScenePlayback"; break;
            case ControlSource::Factory: owner = "Factory"; break;
            case ControlSource::LiveDmx: owner = "LiveDmx"; break;
            case ControlSource::ManualOverride: owner = "ManualOverride"; break;
            case ControlSource::EStop: owner = "EStop"; break;
            default: break;
        }
        cJSON_AddStringToObject(response, "controlOwner", owner);
        cJSON_AddNumberToObject(response, "frameCount", api_ptr->signage()->frame_count());
        cJSON_AddNumberToObject(response, "brightness", api_ptr->signage()->current_brightness());
        cJSON_AddBoolToObject(response, "testActive", api_ptr->runner().status().test_active);

        // Encode DMX telemetry as a compact hex string instead of 512 individual
        // cJSON number objects (~41 KB heap) to prevent OOM on status polls.
        const auto& dmx = api_ptr->signage()->last_dmx_data();
        char dmx_hex[1025];
        for (int i = 0; i < 512; ++i) {
            snprintf(dmx_hex + i * 2, 3, "%02x", dmx[i]);
        }
        dmx_hex[1024] = '\0';
        cJSON_AddStringToObject(response, "dmxHex", dmx_hex);

    } else {
        cJSON_AddStringToObject(response, "signage", "not_started");
    }

    if (api_ptr->config()) {
        DeviceConfig cfg;
        api_ptr->config()->load_config(cfg);
    }

    cJSON_AddNumberToObject(
        response,
        "controlSource",
        static_cast<int>(api_ptr->signage() ? api_ptr->signage()->current_control_source() : api_ptr->runner().control_sources().current_owner())
    );

    cJSON_AddBoolToObject(
        response,
        "estopActive",
        api_ptr->signage() && api_ptr->signage()->is_estop_active()
    );

    cJSON_AddBoolToObject(
        response,
        "outputFault",
        api_ptr->signage() && api_ptr->signage()->output_fault()
    );

    cJSON_AddNumberToObject(
        response,
        "outputFaultCount",
        api_ptr->signage() ? api_ptr->signage()->output_fault_count() : 0
    );

    cJSON_AddBoolToObject(
        response,
        "timeSynced",
        api_ptr->scheduler() && api_ptr->scheduler()->has_time()
    );
    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t get_sequence(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    Sequence seq;
    api_ptr->config()->load_sequence(seq);

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "bpm", seq.bpm);
    cJSON_AddBoolToObject(obj, "enabled", seq.enabled);

    cJSON* tracks = cJSON_CreateArray();
    for (uint8_t i = 0; i < 8; ++i) {
        cJSON* trk = cJSON_CreateObject();
        cJSON_AddNumberToObject(trk, "channelId", seq.tracks[i].channel_id);
        cJSON_AddNumberToObject(trk, "numSteps", seq.tracks[i].num_steps);
        cJSON_AddBoolToObject(trk, "enabled", seq.tracks[i].enabled);

        cJSON* steps = cJSON_CreateArray();
        for (uint8_t s = 0; s < 16; ++s) {
            cJSON* stp = cJSON_CreateObject();
            cJSON_AddNumberToObject(stp, "presetIndex", seq.tracks[i].steps[s].preset_index);
            cJSON_AddNumberToObject(stp, "lengthBeats", seq.tracks[i].steps[s].length_beats);
            cJSON_AddItemToArray(steps, stp);
        }
        cJSON_AddItemToObject(trk, "steps", steps);
        cJSON_AddItemToArray(tracks, trk);
    }
    cJSON_AddItemToObject(obj, "tracks", tracks);

    char* json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    esp_err_t ret = send_json(request, json);
    cJSON_free(json);
    return ret;
}

esp_err_t put_sequence(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    Sequence seq{};
    json_u16(root, "bpm", 10, 500, seq.bpm);

    const cJSON* en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (cJSON_IsBool(en)) seq.enabled = cJSON_IsTrue(en);

    const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (cJSON_IsArray(tracks)) {
        int trk_idx = 0;
        cJSON* trk_item = nullptr;
        cJSON_ArrayForEach(trk_item, tracks) {
            if (trk_idx >= 8) break;
            json_u8(trk_item, "channelId", 0, 7, seq.tracks[trk_idx].channel_id);
            json_u8(trk_item, "numSteps", 1, 16, seq.tracks[trk_idx].num_steps);

            const cJSON* trk_en = cJSON_GetObjectItemCaseSensitive(trk_item, "enabled");
            if (cJSON_IsBool(trk_en)) seq.tracks[trk_idx].enabled = cJSON_IsTrue(trk_en);

            const cJSON* steps = cJSON_GetObjectItemCaseSensitive(trk_item, "steps");
            if (cJSON_IsArray(steps)) {
                int stp_idx = 0;
                cJSON* stp_item = nullptr;
                cJSON_ArrayForEach(stp_item, steps) {
                    if (stp_idx >= 16) break;
                    json_u8(stp_item, "presetIndex", 0, kMaxPresets - 1, seq.tracks[trk_idx].steps[stp_idx].preset_index);
                    json_u8(stp_item, "lengthBeats", 1, 64, seq.tracks[trk_idx].steps[stp_idx].length_beats);
                    ++stp_idx;
                }
            }
            ++trk_idx;
        }
    }

    cJSON_Delete(root);

    if (api_ptr->config()->save_sequence(seq) != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    api::AuditLog::instance().log(api::ApiRole::Operator, "sequence_update", "Sequence updated");

    return send_json(request, R"({"saved":true})");
}

esp_err_t get_effects(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    static const char kEffectsJson[] =
        "["
        "{\"id\":\"solid\",\"label\":\"Steady color\",\"description\":\"A constant, even wash of color.\",\"hasParameter\":false},"
        "{\"id\":\"breathe\",\"label\":\"Smooth pulse\",\"description\":\"Gently fades between a soft glow and full brightness.\",\"hasParameter\":true,\"parameterLabel\":\"Minimum glow\",\"parameterUnit\":\"%\",\"parameterMin\":1,\"parameterMax\":100,\"parameterDefault\":4},"
        "{\"id\":\"chase\",\"label\":\"Moving comet\",\"description\":\"A bright comet travels along the strip.\",\"hasParameter\":true,\"parameterLabel\":\"Tail length\",\"parameterUnit\":\"pixels\",\"parameterMin\":1,\"parameterMax\":8,\"parameterDefault\":3},"
        "{\"id\":\"sweep\",\"label\":\"Color sweep\",\"description\":\"Builds color across the strip from end to end.\",\"hasParameter\":false},"
        "{\"id\":\"reveal\",\"label\":\"Soft reveal\",\"description\":\"Fades the lane in for a polished sign-on.\",\"hasParameter\":true,\"parameterLabel\":\"Fade length\",\"parameterUnit\":\"%\",\"parameterMin\":10,\"parameterMax\":90,\"parameterDefault\":25},"
        "{\"id\":\"sparkle\",\"label\":\"Random twinkle\",\"description\":\"Adds crisp white twinkles over the base color.\",\"hasParameter\":true,\"parameterLabel\":\"Twinkles\",\"parameterUnit\":\"per lane\",\"parameterMin\":1,\"parameterMax\":12,\"parameterDefault\":2},"
        "{\"id\":\"gradient\",\"label\":\"Flowing glow\",\"description\":\"Moves a soft wave of light along the strip.\",\"hasParameter\":true,\"parameterLabel\":\"Wave contrast\",\"parameterUnit\":\"%\",\"parameterMin\":0,\"parameterMax\":100,\"parameterDefault\":100},"
        "{\"id\":\"strobe\",\"label\":\"Beat strobe\",\"description\":\"Flashes the lane with an adjustable on-time.\",\"hasParameter\":true,\"parameterLabel\":\"Flash time\",\"parameterUnit\":\"%\",\"parameterMin\":5,\"parameterMax\":95,\"parameterDefault\":50},"
        "{\"id\":\"fire\",\"label\":\"Living flame\",\"description\":\"Creates a flickering flame texture from the selected color.\",\"hasParameter\":true,\"parameterLabel\":\"Flame height\",\"parameterUnit\":\"%\",\"parameterMin\":20,\"parameterMax\":100,\"parameterDefault\":70},"
        "{\"id\":\"aurora\",\"label\":\"Aurora glow\",\"description\":\"Gentle shifting colors across the strip.\",\"hasParameter\":true,\"parameterLabel\":\"Hue shift\",\"parameterUnit\":\"deg\",\"parameterMin\":1,\"parameterMax\":255,\"parameterDefault\":120},"
        "{\"id\":\"colorwave\",\"label\":\"Color wave\",\"description\":\"Smooth sine wave color interpolation.\",\"hasParameter\":true,\"parameterLabel\":\"Wave length\",\"parameterUnit\":\"px\",\"parameterMin\":10,\"parameterMax\":255,\"parameterDefault\":50},"
        "{\"id\":\"staticpulse\",\"label\":\"Static pulse\",\"description\":\"Slow dimmer pulse for main/ambiance lights.\",\"hasParameter\":true,\"parameterLabel\":\"Min dim\",\"parameterUnit\":\"%\",\"parameterMin\":0,\"parameterMax\":100,\"parameterDefault\":20}"
        "]";

    return send_json(request, kEffectsJson);
}

esp_err_t get_calendar_schedule(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    CalendarSchedule schedule{};
    if (api_ptr->config()->load_calendar_schedule(schedule) != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_read_failed");
    }

    cJSON* response = cJSON_CreateObject();
    cJSON* entries = cJSON_CreateArray();
    for (uint8_t index = 0; index < 8; ++index) {
        const CalendarScheduleEntry& entry = schedule.entries[index];
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddNumberToObject(item, "hour", entry.hour);
        cJSON_AddNumberToObject(item, "minute", entry.minute);
        cJSON_AddNumberToObject(item, "daysOfWeek", entry.days_of_week);
        cJSON_AddStringToObject(item, "action", calendar_action_name(entry.action));
        cJSON_AddNumberToObject(item, "presetIndex", entry.preset_index);
        cJSON_AddBoolToObject(item, "enabled", entry.enabled);
        cJSON_AddItemToArray(entries, item);
    }
    cJSON_AddItemToObject(response, "entries", entries);

    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t put_calendar_schedule(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    const cJSON* entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (!cJSON_IsArray(entries)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_schedule_entries");
    }

    CalendarSchedule schedule{};
    int entry_index = 0;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, entries) {
        if (entry_index >= 8) break;

        CalendarScheduleEntry& entry = schedule.entries[entry_index];
        bool valid =
            json_u8(item, "hour", 0, 23, entry.hour) &&
            json_u8(item, "minute", 0, 59, entry.minute) &&
            json_u8(item, "daysOfWeek", 0, 127, entry.days_of_week) &&
            json_u8(item, "presetIndex", 0, kMaxPresets - 1, entry.preset_index);

        const cJSON* action = cJSON_GetObjectItemCaseSensitive(item, "action");
        valid = valid && cJSON_IsString(action) &&
            parse_calendar_action(action->valuestring, entry.action);

        const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        valid = valid && cJSON_IsBool(enabled);
        if (cJSON_IsBool(enabled)) entry.enabled = cJSON_IsTrue(enabled);

        if (!valid) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_schedule_entry");
        }
        ++entry_index;
    }
    cJSON_Delete(root);

    if (api_ptr->config()->save_calendar_schedule(schedule) != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }
    
    api::AuditLog::instance().log(api::ApiRole::Operator, "schedule_update", "Calendar schedule updated");

    return send_json(request, R"({"saved":true})");
}

esp_err_t get_sync_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->node_sync()) {
        return send_error(request, "503 Service Unavailable", "sync_not_ready");
    }

    const NodeSyncStatus status = api_ptr->node_sync()->status();
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "configuredRole", node_role_name(status.configured_role));
    cJSON_AddStringToObject(response, "activeRole", node_role_name(status.active_role));
    cJSON_AddBoolToObject(response, "running", status.running);
    cJSON_AddBoolToObject(response, "clockLocked", status.clock_locked);
    cJSON_AddBoolToObject(response, "clockHasEpoch", status.clock_has_epoch);
    cJSON_AddNumberToObject(response, "peerCount", status.peer_count);
    if (status.last_packet_age_ms == std::numeric_limits<uint32_t>::max()) {
        cJSON_AddNullToObject(response, "lastPacketAgeMs");
    } else {
        cJSON_AddNumberToObject(response, "lastPacketAgeMs", status.last_packet_age_ms);
    }
    cJSON_AddNumberToObject(response, "packetsReceived", status.packets_received);
    cJSON_AddNumberToObject(response, "packetsMissed", status.packets_missed);
    cJSON_AddNumberToObject(response, "clockOffsetMs", status.clock_offset_ms);
    cJSON_AddNumberToObject(response, "clockJitterUs", status.clock_jitter_us);

    cJSON* peers = cJSON_CreateArray();
    for (uint8_t index = 0; index < status.peer_count; ++index) {
        cJSON* peer = cJSON_CreateObject();
        char address[16] = {};
        format_ipv4(status.peers[index].address, address, sizeof(address));
        cJSON_AddStringToObject(peer, "address", address);
        cJSON_AddNumberToObject(peer, "ageMs", status.peers[index].age_ms);
        cJSON_AddNumberToObject(peer, "latencyMs", status.peers[index].latency_ms);
        cJSON_AddItemToArray(peers, peer);
    }
    cJSON_AddItemToObject(response, "peers", peers);

    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}



esp_err_t get_audit_log(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    api::AuditEvent events[50];
    size_t num_events = api::AuditLog::instance().get_events(events, 50);

    cJSON* arr = cJSON_CreateArray();
    for (size_t i = 0; i < num_events; ++i) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "timestamp_s", events[i].timestamp_s);
        cJSON_AddNumberToObject(obj, "role", static_cast<int>(events[i].role));
        cJSON_AddStringToObject(obj, "action", events[i].action);
        cJSON_AddStringToObject(obj, "detail", events[i].detail);
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

#include "dashboard.hpp"

esp_err_t FactoryApi::serve_dashboard(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache, no-store, must-revalidate");

    constexpr size_t kChunkSize = 2048;
    for (size_t offset = 0; offset < dashboard_html_gz_len; offset += kChunkSize) {
        size_t to_send = (dashboard_html_gz_len - offset > kChunkSize) ? kChunkSize : (dashboard_html_gz_len - offset);
        esp_err_t err = httpd_resp_send_chunk(request, reinterpret_cast<const char*>(dashboard_html_gz + offset), to_send);
        if (err != ESP_OK) {
            httpd_resp_send_chunk(request, nullptr, 0);
            return err;
        }
    }
    return httpd_resp_send_chunk(request, nullptr, 0);
}

FactoryApi::FactoryApi(FactoryRunner& runner) : runner_(runner) {}


// ── Phase 4: OTA & Config Export/Import ──

esp_err_t post_ota(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        return send_error(request, "500 Internal Server Error", "No OTA partition found");
    }

    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return send_error(request, "500 Internal Server Error", "OTA begin failed");
    }

    char buf[1024];
    int received = 0;
    int remaining = request->content_len;

    while (remaining > 0) {
        if ((received = httpd_req_recv(request, buf, std::min(remaining, (int)sizeof(buf)))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            esp_ota_abort(update_handle);
            return send_error(request, "500 Internal Server Error", "OTA receive failed");
        }
        err = esp_ota_write(update_handle, buf, received);
        if (err != ESP_OK) {
            esp_ota_abort(update_handle);
            return send_error(request, "500 Internal Server Error", "OTA write failed");
        }
        remaining -= received;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "OTA end failed");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "OTA set boot failed");
    }

    send_json(request, "{\"status\":\"success\",\"message\":\"OTA update successful, rebooting...\"}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t get_backup(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) {
        return send_error(request, "500 Internal Server Error", "ConfigStore not available");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 2);
    
    // Config
    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);
    cJSON* cfg_json = cJSON_CreateObject();
    cJSON_AddStringToObject(cfg_json, "deviceName", cfg.device_name);
    cJSON_AddNumberToObject(cfg_json, "activePresetIndex", cfg.active_preset_index);
    cJSON_AddNumberToObject(cfg_json, "fallbackPresetIndex", cfg.fallback_preset_index);
    cJSON_AddNumberToObject(cfg_json, "watchdogTimeoutS", cfg.watchdog_timeout_s);
    cJSON_AddNumberToObject(cfg_json, "maxPowerMw", cfg.max_power_mw);
    cJSON_AddNumberToObject(cfg_json, "gammaX10", cfg.gamma_x10);
    cJSON_AddBoolToObject(cfg_json, "softStartEnabled", cfg.soft_start_enabled);
    cJSON_AddNumberToObject(cfg_json, "softStartMs", cfg.soft_start_ms);
    cJSON_AddBoolToObject(cfg_json, "mqttEnabled", cfg.mqtt_enabled);
    cJSON_AddStringToObject(cfg_json, "mqttBroker", cfg.mqtt_broker);
    cJSON_AddNumberToObject(cfg_json, "mqttPort", cfg.mqtt_port);
    cJSON_AddBoolToObject(cfg_json, "syncEnabled", cfg.sync_enabled);
    cJSON_AddNumberToObject(cfg_json, "syncRole", cfg.sync_role);

    // Protocol config
    cJSON* prot_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(prot_json, "artnetEnabled", cfg.protocol.artnet_enabled);
    cJSON_AddBoolToObject(prot_json, "sacnEnabled", cfg.protocol.sacn_enabled);
    cJSON_AddNumberToObject(prot_json, "universeStart", cfg.protocol.universe_start);
    cJSON_AddNumberToObject(prot_json, "liveStreamLayer", cfg.protocol.live_stream_layer);
    cJSON_AddItemToObject(cfg_json, "protocol", prot_json);

    // Tokens
    cJSON* auth_json = cJSON_CreateObject();
    cJSON_AddStringToObject(auth_json, "adminToken", cfg.admin_token);
    cJSON_AddStringToObject(auth_json, "installerToken", cfg.installer_token);
    cJSON_AddStringToObject(auth_json, "operatorToken", cfg.operator_token);
    cJSON_AddItemToObject(cfg_json, "auth", auth_json);

    // Zones
    cJSON* zones_arr = cJSON_CreateArray();
    for (uint8_t z = 0; z < 8; ++z) {
        cJSON* zj = cJSON_CreateObject();
        cJSON_AddStringToObject(zj, "name", cfg.zones[z].name);
        cJSON_AddStringToObject(zj, "role", zone_role_name(cfg.zones[z].role));
        cJSON_AddItemToArray(zones_arr, zj);
    }
    cJSON_AddItemToObject(cfg_json, "zones", zones_arr);

    cJSON_AddItemToObject(root, "config", cfg_json);

    // Presets
    cJSON* presets_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < lg::kMaxPresets; ++i) {
        SceneConfig sc;
        if (api_ptr->config()->load_scene(i, sc) == ESP_OK) {
            cJSON* s = cJSON_CreateObject();
            cJSON_AddNumberToObject(s, "index", i);
            cJSON_AddStringToObject(s, "name", sc.name);
            cJSON_AddNumberToObject(s, "globalBrightness", sc.global_brightness);
            cJSON_AddNumberToObject(s, "transitionMs", sc.transition_ms);

            cJSON* z_arr = cJSON_CreateArray();
            for (uint8_t z = 0; z < 8; ++z) {
                cJSON* z_obj = cJSON_CreateObject();
                cJSON* l_arr = cJSON_CreateArray();
                for (uint8_t l = 0; l < 3; ++l) {
                    const auto& layer = sc.zones[z].layers[l];
                    cJSON* l_obj = cJSON_CreateObject();
                    cJSON_AddBoolToObject(l_obj, "enabled", layer.enabled);
                    cJSON_AddNumberToObject(l_obj, "effect", static_cast<int>(layer.effect));
                    cJSON_AddNumberToObject(l_obj, "speedMs", layer.speed_ms);
                    
                    cJSON* pcol = cJSON_CreateObject();
                    cJSON_AddNumberToObject(pcol, "r", layer.primary_color.r);
                    cJSON_AddNumberToObject(pcol, "g", layer.primary_color.g);
                    cJSON_AddNumberToObject(pcol, "b", layer.primary_color.b);
                    cJSON_AddItemToObject(l_obj, "primaryColor", pcol);

                    cJSON* scol = cJSON_CreateObject();
                    cJSON_AddNumberToObject(scol, "r", layer.secondary_color.r);
                    cJSON_AddNumberToObject(scol, "g", layer.secondary_color.g);
                    cJSON_AddNumberToObject(scol, "b", layer.secondary_color.b);
                    cJSON_AddItemToObject(l_obj, "secondaryColor", scol);

                    cJSON_AddNumberToObject(l_obj, "opacity", layer.opacity);
                    cJSON_AddNumberToObject(l_obj, "blendMode", static_cast<int>(layer.blend_mode));
                    cJSON_AddNumberToObject(l_obj, "parameter", layer.fx_param);
                    cJSON_AddItemToArray(l_arr, l_obj);
                }
                cJSON_AddItemToObject(z_obj, "layers", l_arr);
                cJSON_AddItemToArray(z_arr, z_obj);
            }
            cJSON_AddItemToObject(s, "zones", z_arr);

            // Legacy v1 proxy fields
            cJSON_AddNumberToObject(s, "effect", sc.zones[0].layers[0].effect);
            cJSON_AddNumberToObject(s, "speedMs", sc.zones[0].layers[0].speed_ms);
            
            cJSON_AddItemToArray(presets_arr, s);
        }
    }
    cJSON_AddItemToObject(root, "presets", presets_arr);

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Content-Disposition", "attachment; filename=\"lumigrid_backup.json\"");
    esp_err_t err = httpd_resp_sendstr(request, json_str);
    cJSON_free(json_str);
    return err;
}

esp_err_t post_restore(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) {
        return send_error(request, "500 Internal Server Error", "ConfigStore not available");
    }

    if (request->content_len <= 0 || request->content_len > 32768) {
        return send_error(request, "400 Bad Request", "Payload too large or empty");
    }

    char* buf = (char*)malloc(request->content_len + 1);
    if (!buf) return send_error(request, "500 Internal Server Error", "Out of memory");

    int received = 0;
    while (received < request->content_len) {
        int ret = httpd_req_recv(request, buf + received, request->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (ret <= 0) {
            free(buf);
            return send_error(request, "500 Internal Server Error", "Failed to receive payload");
        }
        received += ret;
    }
    buf[received] = '\0';

    cJSON* root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        return send_error(request, "400 Bad Request", "Invalid JSON");
    }

    // Config
    cJSON* cfg_json = cJSON_GetObjectItem(root, "config");
    if (cfg_json) {
        DeviceConfig cfg;
        api_ptr->config()->load_config(cfg);
        
        lg::json::get_string(cfg_json, "deviceName", cfg.device_name, sizeof(cfg.device_name));
        lg::json::get_number(cfg_json, "maxPowerMw", cfg.max_power_mw);
        lg::json::get_number(cfg_json, "gammaX10", cfg.gamma_x10);
        lg::json::get_number(cfg_json, "mqttEnabled", cfg.mqtt_enabled);
        lg::json::get_string(cfg_json, "mqttBroker", cfg.mqtt_broker, sizeof(cfg.mqtt_broker));
        lg::json::get_number(cfg_json, "mqttPort", cfg.mqtt_port);
        lg::json::get_number(cfg_json, "syncEnabled", cfg.sync_enabled);
        lg::json::get_number(cfg_json, "syncRole", cfg.sync_role);

        cJSON* prot = cJSON_GetObjectItem(cfg_json, "protocol");
        if (prot) {
            lg::json::get_bool(prot, "artnetEnabled", cfg.protocol.artnet_enabled);
            lg::json::get_bool(prot, "sacnEnabled", cfg.protocol.sacn_enabled);
            lg::json::get_number(prot, "universeStart", cfg.protocol.universe_start);
            lg::json::get_number(prot, "liveStreamLayer", cfg.protocol.live_stream_layer);
        }

        cJSON* auth = cJSON_GetObjectItem(cfg_json, "auth");
        if (auth) {
            lg::json::get_string(auth, "adminToken", cfg.admin_token, sizeof(cfg.admin_token));
            lg::json::get_string(auth, "installerToken", cfg.installer_token, sizeof(cfg.installer_token));
            lg::json::get_string(auth, "operatorToken", cfg.operator_token, sizeof(cfg.operator_token));
        }

        cJSON* z_arr = cJSON_GetObjectItem(cfg_json, "zones");
        if (z_arr && cJSON_IsArray(z_arr)) {
            int z_idx = 0;
            cJSON* zj;
            cJSON_ArrayForEach(zj, z_arr) {
                if (z_idx >= 8) break;
                char role_buf[32] = "";
                lg::json::get_string(zj, "name", cfg.zones[z_idx].name, sizeof(cfg.zones[z_idx].name));
                if (lg::json::get_string(zj, "role", role_buf, sizeof(role_buf))) {
                    parse_zone_role(role_buf, cfg.zones[z_idx].role);
                }
                z_idx++;
            }
        }
        
        api_ptr->config()->save_config(cfg);
        api::update_tokens(cfg.admin_token, cfg.installer_token, cfg.operator_token);
    }

    // Presets
    cJSON* presets_arr = cJSON_GetObjectItem(root, "presets");
    if (presets_arr && cJSON_IsArray(presets_arr)) {
        cJSON* p;
        cJSON_ArrayForEach(p, presets_arr) {
            cJSON* idx = cJSON_GetObjectItem(p, "index");
            if (idx && idx->valueint >= 0 && idx->valueint < lg::kMaxPresets) {
                SceneConfig sc;
                lg::json::get_string(p, "name", sc.name, sizeof(sc.name));
                lg::json::get_number(p, "globalBrightness", sc.global_brightness);
                lg::json::get_number(p, "transitionMs", sc.transition_ms);

                cJSON* z_arr = cJSON_GetObjectItem(p, "zones");
                if (z_arr && cJSON_IsArray(z_arr)) {
                    int z_idx = 0;
                    cJSON* zj;
                    cJSON_ArrayForEach(zj, z_arr) {
                        if (z_idx >= 8) break;
                        cJSON* l_arr = cJSON_GetObjectItem(zj, "layers");
                        if (l_arr && cJSON_IsArray(l_arr)) {
                            int l_idx = 0;
                            cJSON* lj;
                            cJSON_ArrayForEach(lj, l_arr) {
                                if (l_idx >= 3) break;
                                auto& layer = sc.zones[z_idx].layers[l_idx];
                                lg::json::get_bool(lj, "enabled", layer.enabled);
                                lg::json::get_number(lj, "effect", layer.effect);
                                lg::json::get_number(lj, "speedMs", layer.speed_ms);
                                lg::json::get_number(lj, "opacity", layer.opacity);
                                
                                int blend = 0;
                                if (lg::json::get_number(lj, "blendMode", blend)) {
                                    layer.blend_mode = static_cast<BlendMode>(blend);
                                }
                                lg::json::get_number(lj, "parameter", layer.fx_param);

                                cJSON* pcol = cJSON_GetObjectItem(lj, "primaryColor");
                                if (pcol) {
                                    lg::json::get_number(pcol, "r", layer.primary_color.r);
                                    lg::json::get_number(pcol, "g", layer.primary_color.g);
                                    lg::json::get_number(pcol, "b", layer.primary_color.b);
                                }
                                cJSON* scol = cJSON_GetObjectItem(lj, "secondaryColor");
                                if (scol) {
                                    lg::json::get_number(scol, "r", layer.secondary_color.r);
                                    lg::json::get_number(scol, "g", layer.secondary_color.g);
                                    lg::json::get_number(scol, "b", layer.secondary_color.b);
                                }
                                l_idx++;
                            }
                        }
                        z_idx++;
                    }
                } else {
                    // Fallback for v1 legacy backups
                    lg::json::get_number(p, "effect", sc.zones[0].layers[0].effect);
                    lg::json::get_number(p, "speedMs", sc.zones[0].layers[0].speed_ms);
                }
                
                api_ptr->config()->save_scene(idx->valueint, sc);
            }
        }
    }
    
    cJSON_Delete(root);

    api::AuditLog::instance().log(api::ApiRole::Admin, "RESTORE", "Configuration restored via API");

    send_json(request, "{\"status\":\"success\",\"message\":\"Restore successful, rebooting...\"}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

struct WsBroadcastArg {
    FactoryApi* api;
    char payload[384];
};

struct WsPreviewArg {
    FactoryApi* api;
    std::vector<uint8_t> payload;
};

static esp_err_t get_favicon(httpd_req_t* request) {
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t FactoryApi::start(
    uint16_t signage_port,
    uint16_t factory_port,
    ConfigStore* config,
    LightingRuntime* signage,
    Scheduler* scheduler,
    NodeSync* node_sync,
    DmxRouter* dmx_router
) {
    config_ = config;
    signage_ = signage;
    scheduler_ = scheduler;
    node_sync_ = node_sync;
    dmx_router_ = dmx_router;

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 40;
    http_config.stack_size = 8192;
    http_config.lru_purge_enable = true;
    http_config.recv_wait_timeout = 10;
    http_config.send_wait_timeout = 10;
    http_config.server_port = signage_port;
    ESP_RETURN_ON_ERROR(httpd_start(&signage_server_, &http_config), "factory-api", "start server");
    factory_server_ = signage_server_;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const httpd_uri_t factory_routes[] = {
        // ── Factory test endpoints ──
        { .uri = "/api/v1/factory/status",    .method = HTTP_GET,  .handler = get_status,     .user_ctx = this },
        { .uri = "/api/v1/factory/effect",    .method = HTTP_POST, .handler = post_effect,    .user_ctx = this },
        { .uri = "/api/v1/factory/lane",      .method = HTTP_POST, .handler = post_lane,      .user_ctx = this },
        { .uri = "/api/v1/factory/pwm",       .method = HTTP_POST, .handler = post_pwm,       .user_ctx = this },
        { .uri = "/api/v1/factory/abort",     .method = HTTP_POST, .handler = post_abort,     .user_ctx = this },
        { .uri = "/api/v1/factory/auto-loop", .method = HTTP_POST, .handler = post_auto_loop, .user_ctx = this },
    };

    const httpd_uri_t signage_routes[] = {
        // ── WebSocket + Dashboard ──
        { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = this,
          .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr },
        { .uri = "/",   .method = HTTP_GET, .handler = serve_dashboard, .user_ctx = this },
        { .uri = "/favicon.ico", .method = HTTP_GET, .handler = get_favicon, .user_ctx = this },
        
        // ── Signage endpoints ──
        { .uri = "/api/v1/presets",           .method = HTTP_GET,  .handler = get_presets,      .user_ctx = this },
        { .uri = "/api/v1/presets",           .method = HTTP_PUT,  .handler = put_preset,       .user_ctx = this },
        { .uri = "/api/v1/auth/update",       .method = HTTP_POST, .handler = post_auth_update, .user_ctx = this },
        { .uri = "/api/v1/activate",          .method = HTTP_POST, .handler = post_activate,    .user_ctx = this },
        { .uri = "/api/v1/config",            .method = HTTP_GET,  .handler = get_device_config, .user_ctx = this },
        { .uri = "/api/v1/config",            .method = HTTP_PUT,  .handler = put_device_config, .user_ctx = this },
        { .uri = "/api/v1/status",            .method = HTTP_GET,  .handler = get_signage_status, .user_ctx = this },
        { .uri = "/api/v1/effects",           .method = HTTP_GET,  .handler = get_effects,        .user_ctx = this },
        { .uri = "/api/v1/health",            .method = HTTP_GET,  .handler = get_health,         .user_ctx = this },
        { .uri = "/api/v1/logs",              .method = HTTP_GET,  .handler = get_logs,           .user_ctx = this },
        { .uri = "/api/v1/sequence",          .method = HTTP_GET,  .handler = get_sequence,     .user_ctx = this },
        { .uri = "/api/v1/sequence",          .method = HTTP_PUT,  .handler = put_sequence,     .user_ctx = this },
        { .uri = "/api/v1/schedules",         .method = HTTP_GET,  .handler = get_calendar_schedule, .user_ctx = this },
        { .uri = "/api/v1/schedules",         .method = HTTP_PUT,  .handler = put_calendar_schedule, .user_ctx = this },
        { .uri = "/api/v1/sync",              .method = HTTP_GET,  .handler = get_sync_status,     .user_ctx = this },
        { .uri = "/api/v1/audit",             .method = HTTP_GET,  .handler = get_audit_log,       .user_ctx = this },
        { .uri = "/api/v1/ota",               .method = HTTP_POST, .handler = post_ota,           .user_ctx = this },
        { .uri = "/api/v1/backup",            .method = HTTP_GET,  .handler = get_backup,         .user_ctx = this },
        { .uri = "/api/v1/restore",           .method = HTTP_POST, .handler = post_restore,       .user_ctx = this },
    };


    for (const auto& endpoint : factory_routes) {
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(signage_server_, &endpoint),
            "api",
            "register uri"
        );
    }
    for (const auto& endpoint : signage_routes) {
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(signage_server_, &endpoint),
            "api",
            "register uri"
        );
    }

    runner_.set_status_callback([this]() { this->notify_status_async(); });
    
    if (signage_) {
        signage_->set_preview_callback([this](const RenderFrame& frame, const DeviceConfig& cfg) {
            if (this->factory_server_ == nullptr) return;

            // Rate limit to ~10 fps (100ms)
            static uint32_t last_preview_ms = 0;
            uint32_t now = esp_log_timestamp();
            if (now - last_preview_ms < 100) return;
            
            bool expected = false;
            if (!this->preview_pending_.compare_exchange_strong(expected, true)) {
                return; // Already pending
            }
            last_preview_ms = now;

            // Only generate preview frames if there are active WebSocket clients connected
            size_t client_count = 4;
            int client_fds[4];
            if (httpd_get_client_list(this->factory_server_, &client_count, client_fds) != ESP_OK || client_count == 0) {
                this->preview_pending_ = false;
                return;
            }

            auto* arg = new (std::nothrow) WsPreviewArg{};
            if (!arg) {
                this->preview_pending_ = false;
                return;
            }
            arg->api = this;
            
            try {
                // Fixed payload size: 1 byte header + 8 lanes * 144 pixels * 3 colors = 3457 bytes
                arg->payload.resize(3457, 0);
                arg->payload[0] = 0x01; // Header

                for (uint8_t f = 0; f < 8; ++f) {
                    const auto& fix = cfg.fixtures[f];
                    const auto& ff = frame.fixtures[f];
                    bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
                    
                    if (is_addressable && fix.num_pixels > 0) {
                        uint16_t pcount = std::min<uint16_t>(fix.num_pixels, 144);
                        size_t offset = 1 + (f * 144 * 3);
                        for (uint16_t p = 0; p < pcount; ++p) {
                            const auto& lv = ff.pixels[p];
                            arg->payload[offset++] = (std::min)(255, int(lv.r) + lv.w);
                            arg->payload[offset++] = (std::min)(255, int(lv.g) + lv.w);
                            arg->payload[offset++] = (std::min)(255, int(lv.b) + lv.w);
                        }
                    }
                }

                if (httpd_queue_work(this->factory_server_, ws_broadcast_preview_work, arg) != ESP_OK) {
                    this->preview_pending_ = false;
                    delete arg;
                }
            } catch (...) {
                this->preview_pending_ = false;
                delete arg;
            }
        });
    }

    return ESP_OK;
}

void FactoryApi::ws_broadcast_work(void* arg) {
    std::unique_ptr<WsBroadcastArg> msg(static_cast<WsBroadcastArg*>(arg));

    std::vector<int> client_fds(msg->api->max_open_sockets_);
    size_t clients = client_fds.size();

    if (httpd_get_client_list(msg->api->factory_server_, &clients, client_fds.data()) != ESP_OK) {
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(msg->payload);
    frame.len = strlen(msg->payload);

    for (size_t i = 0; i < clients; ++i) {
        if (httpd_ws_get_fd_info(msg->api->factory_server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(msg->api->factory_server_, client_fds[i], &frame);
        }
    }
}

void FactoryApi::ws_broadcast_preview_work(void* arg) {
    std::unique_ptr<WsPreviewArg> msg(static_cast<WsPreviewArg*>(arg));
    msg->api->preview_pending_ = false;

    std::vector<int> client_fds(msg->api->max_open_sockets_);
    size_t clients = client_fds.size();

    if (httpd_get_client_list(msg->api->factory_server_, &clients, client_fds.data()) != ESP_OK || clients == 0) {
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = msg->payload.data();
    frame.len = msg->payload.size();

    for (size_t i = 0; i < clients; ++i) {
        if (httpd_ws_get_fd_info(msg->api->factory_server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(msg->api->factory_server_, client_fds[i], &frame);
        }
    }
}

void FactoryApi::notify_status_async() {
    if (factory_server_ == nullptr) return;

    auto* arg = new WsBroadcastArg{};
    arg->api = this;
    build_status_json(runner_.status(), arg->payload, sizeof(arg->payload));

    if (httpd_queue_work(factory_server_, ws_broadcast_work, arg) != ESP_OK) {
        delete arg;
    }
}

void FactoryApi::stop() {
    if (signage_server_ != nullptr) {
        httpd_stop(signage_server_);
        signage_server_ = nullptr;
        factory_server_ = nullptr;
    }
}

} // namespace lg
