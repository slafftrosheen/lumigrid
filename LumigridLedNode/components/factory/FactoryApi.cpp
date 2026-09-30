// components/factory/FactoryApi.cpp
#include "FactoryApi.hpp"
#include "RuntimeSnapshot.hpp"
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
#include "NetworkSettings.hpp"
#include "WifiService.hpp"
#include "LightingRuntime.hpp"
#include "SignageTypes.hpp"
#include "ChannelHardware.hpp"
#include "Scheduler.hpp"
#include "NodeSync.hpp"
#include "DmxRouter.hpp"
#include "esp_check.h"
#include "NodeIdentity.hpp"
#include "FleetDiscoveryService.hpp"
#include "Capabilities.hpp"
#include "CueRunner.hpp"
#include "ChannelConfiguration.hpp"
#include "ShowTransport.hpp"
#include <inttypes.h>
#include <cmath>

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
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    return httpd_resp_sendstr(request, body);
}

esp_err_t send_json(httpd_req_t* request, cJSON* json, const char* status = "200 OK") {
    char* body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return ESP_FAIL;
    esp_err_t res = send_json(request, body, status);
    cJSON_free(body);
    return res;
}

esp_err_t send_error(httpd_req_t* request,
                     const char* status,
                     const char* code) {
    char body[128];
    snprintf(body, sizeof(body), R"({"error":"%s"})", code);
    return send_json(request, body, status);
}

inline bool channel_is_configured(const ChannelHardwareDescriptor& hw, const ChannelConfig& cfg) {
    return channel_is_configured(hw.index, cfg);
}

void format_ipv4(uint32_t address, char* output, size_t output_size);

static bool is_sync_follower_read_only(FactoryApi* api_ptr, char* out_leader, size_t leader_len) {
    if (!api_ptr || !api_ptr->node_sync()) return false;
    auto* sync = api_ptr->node_sync();
    if (sync->active_role() != NodeRole::Worker) return false;
    auto st = sync->status();
    if (st.master_address != 0 && st.last_packet_age_ms < 3000) {
        if (out_leader && leader_len > 0) {
            format_ipv4(st.master_address, out_leader, leader_len);
        }
        return true;
    }
    return false;
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
        .command_id = 0,
        .type = FactoryCommandType::SelectEffect,
        .effect = effect,
        .brightness = brightness,
        .duration_ms = duration_ms
    };

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Factory);
    command.command_id = cmd_id;
    if (!api_ptr->runner().enqueue(command)) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "202 Accepted");
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
        .command_id = 0,
        .type = FactoryCommandType::PixelLaneSolid,
        .lane_or_channel = lane,
        .red = red,
        .green = green,
        .blue = blue,
        .brightness = brightness,
        .duration_ms = duration_ms
    };

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Factory);
    command.command_id = cmd_id;
    if (!api_ptr->runner().enqueue(command)) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "202 Accepted");
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
        .command_id = 0,
        .type = FactoryCommandType::PwmChannelLevel,
        .lane_or_channel = channel,
        .pwm_duty = duty,
        .duration_ms = duration_ms
    };

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Factory);
    command.command_id = cmd_id;
    if (!api_ptr->runner().enqueue(command)) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "202 Accepted");
}

esp_err_t post_abort(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Factory);
    if (!api_ptr->runner().enqueue({
            .command_id = cmd_id,
            .type = FactoryCommandType::Abort
        })) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 ",\"action\":\"blackout\"}", cmd_id);
    return send_json(request, resp, "202 Accepted");
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
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Factory);
    if (!api_ptr->runner().enqueue({
            .command_id = cmd_id,
            .type = FactoryCommandType::SetAutoLoop,
            .enabled = value
        })) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "202 Accepted");
}

esp_err_t ws_handler(httpd_req_t* request) {
    if (request->method == HTTP_GET) {
        char proto_hdr[128] = {};
        if (httpd_req_get_hdr_value_str(request, "Sec-WebSocket-Protocol", proto_hdr, sizeof(proto_hdr)) != ESP_OK) {
            api::AuditLog::instance().log(api::ApiRole::Operator, "ws_auth_failed", "WebSocket missing ticket protocol");
            httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "Missing Sec-WebSocket-Protocol");
            return ESP_FAIL;
        }

        const char* prefix = "lumigrid-ticket.";
        char* ticket_pos = strstr(proto_hdr, prefix);
        if (!ticket_pos) {
            api::AuditLog::instance().log(api::ApiRole::Operator, "ws_auth_failed", "WebSocket invalid ticket protocol");
            httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "Invalid WebSocket ticket protocol");
            return ESP_FAIL;
        }
        ticket_pos += strlen(prefix);
        char ticket[33] = {};
        size_t t_len = 0;
        while (ticket_pos[t_len] && ticket_pos[t_len] != ',' && ticket_pos[t_len] != ' ' && t_len < 32) {
            ticket[t_len] = ticket_pos[t_len];
            t_len++;
        }
        ticket[t_len] = '\0';

        auto* auth_mgr = api::get_auth_manager();
        auth::AuthRole role = auth::AuthRole::Operator;
        if (!auth_mgr || !auth_mgr->consume_ws_ticket(ticket, &role)) {
            api::AuditLog::instance().log(api::ApiRole::Operator, "ws_auth_failed", "WebSocket invalid or expired ticket");
            httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "Invalid or expired ticket");
            return ESP_FAIL;
        }

        // Echo the negotiated subprotocol back so client completes handshake
        char subproto_resp[64];
        snprintf(subproto_resp, sizeof(subproto_resp), "lumigrid-ticket.%s", ticket);
        httpd_resp_set_hdr(request, "Sec-WebSocket-Protocol", subproto_resp);
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

esp_err_t get_info(httpd_req_t* request) {
    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    DeviceConfig cfg;
    const char* dev_name = nullptr;
    if (api_ptr->config() && api_ptr->config()->load_config(cfg) == ESP_OK) {
        dev_name = cfg.device_name;
    }
    NodeIdentity id = get_node_identity(dev_name);
    char body[384];
    snprintf(body, sizeof(body),
        "{\"id\":\"%s\",\"name\":\"%s\",\"family\":\"%s\",\"model\":\"%s\",\"hardwareRevision\":\"%s\",\"firmwareVersion\":\"%s\"}",
        id.id, id.name, id.family, id.model, id.hw_revision, id.fw_version);
    return send_json(request, body);
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
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key");
    esp_err_t err = httpd_resp_sendstr(request, logs);
    
    delete[] logs;
    return err;
}

esp_err_t get_auth_status(httpd_req_t* request) {
    auto* auth_mgr = api::get_auth_manager();
    bool prov = auth_mgr ? auth_mgr->is_provisioned() : false;
    bool allowed = auth_mgr ? auth_mgr->is_bootstrap_allowed() : false;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "provisioned", prov);
    cJSON_AddBoolToObject(root, "bootstrapAllowed", allowed);
    return send_json(request, root, "200 OK");
}

esp_err_t post_auth_bootstrap(httpd_req_t* request) {
    auto* auth_mgr = api::get_auth_manager();
    if (!auth_mgr || auth_mgr->is_provisioned()) {
        return send_error(request, "400 Bad Request", "already_provisioned");
    }
    if (!auth_mgr->is_bootstrap_allowed()) {
        return send_error(request, "403 Forbidden", "bootstrap_not_allowed");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !root) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    cJSON* tok = cJSON_GetObjectItem(root, "adminToken");
    if (!tok || !cJSON_IsString(tok) || strlen(tok->valuestring) < 16 || strlen(tok->valuestring) > 64) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_token_length");
    }

    esp_err_t err = auth_mgr->bootstrap(tok->valuestring);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "bootstrap_failed");
    }

    api::AuditLog::instance().log(api::ApiRole::Admin, "auth_bootstrap", "Admin credential bootstrapped");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddBoolToObject(resp, "provisioned", true);
    return send_json(request, resp, "200 OK");
}

esp_err_t post_auth_session(httpd_req_t* request) {
    auto* auth_mgr = api::get_auth_manager();
    if (!auth_mgr) {
        return send_error(request, "503 Service Unavailable", "auth_unavailable");
    }

    if (auth_mgr->is_rate_limited()) {
        return send_error(request, "429 Too Many Requests", "rate_limited");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !root) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    cJSON* tok = cJSON_GetObjectItem(root, "token");
    if (!tok || !cJSON_IsString(tok)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "token_required");
    }

    char session_hex[65] = {};
    lg::auth::AuthRole role = lg::auth::AuthRole::Operator;
    uint32_t expires_in = 0;

    esp_err_t err = auth_mgr->create_session(tok->valuestring, session_hex, &role, &expires_in);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        auth_mgr->record_failed_attempt();
        api::AuditLog::instance().log(api::ApiRole::Operator, "auth_login_failed", "Invalid credentials");
        return send_error(request, "401 Unauthorized", "invalid_credentials");
    }

    auth_mgr->record_successful_attempt();
    api::AuditLog::instance().log(static_cast<api::ApiRole>(role), "auth_login_success", "Session established");

    const char* role_str = (role == lg::auth::AuthRole::Admin) ? "admin"
                         : (role == lg::auth::AuthRole::Installer) ? "installer"
                         : "operator";

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "authenticated", true);
    cJSON_AddStringToObject(resp, "role", role_str);
    cJSON_AddStringToObject(resp, "sessionToken", session_hex);
    cJSON_AddNumberToObject(resp, "expiresIn", expires_in);
    return send_json(request, resp, "200 OK");
}

esp_err_t get_auth_session(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    auto principal = api::authenticate(request);
    const char* role_str = (principal.role == api::ApiRole::Admin) ? "admin"
                         : (principal.role == api::ApiRole::Installer) ? "installer"
                         : "operator";

    uint32_t rem = 1800;
    char auth_hdr[128] = {};
    size_t hdr_len = httpd_req_get_hdr_value_len(request, "Authorization");
    if (hdr_len > 0 && hdr_len < sizeof(auth_hdr)) {
        if (httpd_req_get_hdr_value_str(request, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
            char* token = auth_hdr;
            if (strncasecmp(token, "Bearer ", 7) == 0) token += 7;
            while (*token == ' ' || *token == '\t') token++;
            size_t len = std::strlen(token);
            while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\r' || token[len - 1] == '\n' || token[len - 1] == '\t')) {
                token[len - 1] = '\0';
                len--;
            }
            auto* auth_mgr = api::get_auth_manager();
            if (auth_mgr) {
                auto res = auth_mgr->inspect_session(token);
                if (res.valid) rem = res.remaining_s;
            }
        }
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "authenticated", true);
    cJSON_AddStringToObject(resp, "role", role_str);
    cJSON_AddNumberToObject(resp, "expiresIn", rem);
    return send_json(request, resp, "200 OK");
}

esp_err_t delete_auth_session(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    char auth_hdr[128] = {};
    size_t hdr_len = httpd_req_get_hdr_value_len(request, "Authorization");
    if (hdr_len > 0 && hdr_len < sizeof(auth_hdr)) {
        if (httpd_req_get_hdr_value_str(request, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
            char* token = auth_hdr;
            if (strncasecmp(token, "Bearer ", 7) == 0) token += 7;
            while (*token == ' ' || *token == '\t') token++;
            size_t len = std::strlen(token);
            while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\r' || token[len - 1] == '\n' || token[len - 1] == '\t')) {
                token[len - 1] = '\0';
                len--;
            }
            auto* auth_mgr = api::get_auth_manager();
            if (auth_mgr) {
                auth_mgr->delete_session(token);
            }
        }
    }

    api::AuditLog::instance().log(api::ApiRole::Operator, "auth_logout", "Session ended");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddBoolToObject(resp, "loggedOut", true);
    return send_json(request, resp, "200 OK");
}

esp_err_t post_auth_ws_ticket(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    auto principal = api::authenticate(request);
    auto* auth_mgr = api::get_auth_manager();
    if (!auth_mgr) {
        return send_error(request, "503 Service Unavailable", "auth_unavailable");
    }

    char ticket_hex[33] = {};
    uint32_t expires_in_ms = 0;
    esp_err_t err = auth_mgr->create_ws_ticket(static_cast<lg::auth::AuthRole>(principal.role), ticket_hex, &expires_in_ms);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "ticket_generation_failed");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "ticket", ticket_hex);
    cJSON_AddNumberToObject(resp, "expiresInMs", expires_in_ms);
    return send_json(request, resp, "200 OK");
}

esp_err_t get_auth_credentials(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    auto* auth_mgr = api::get_auth_manager();
    bool admin_en = false, inst_en = false, op_en = false;
    if (auth_mgr) {
        auth_mgr->get_credential_status(admin_en, inst_en, op_en);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON* a = cJSON_CreateObject();
    cJSON_AddBoolToObject(a, "enabled", admin_en);
    cJSON_AddItemToObject(resp, "admin", a);

    cJSON* i = cJSON_CreateObject();
    cJSON_AddBoolToObject(i, "enabled", inst_en);
    cJSON_AddItemToObject(resp, "installer", i);

    cJSON* o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", op_en);
    cJSON_AddItemToObject(resp, "operator", o);

    return send_json(request, resp, "200 OK");
}

esp_err_t post_auth_credentials(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !root) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    cJSON* role_item = cJSON_GetObjectItem(root, "role");
    cJSON* token_item = cJSON_GetObjectItem(root, "token");
    if (!role_item || !cJSON_IsString(role_item) || !token_item || !cJSON_IsString(token_item)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_role_or_token");
    }

    if (strlen(token_item->valuestring) < 16 || strlen(token_item->valuestring) > 64) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_token_length");
    }

    lg::auth::AuthRole role = lg::auth::AuthRole::Operator;
    if (strcmp(role_item->valuestring, "admin") == 0) {
        role = lg::auth::AuthRole::Admin;
    } else if (strcmp(role_item->valuestring, "installer") == 0) {
        role = lg::auth::AuthRole::Installer;
    } else if (strcmp(role_item->valuestring, "operator") == 0) {
        role = lg::auth::AuthRole::Operator;
    } else {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_role");
    }

    auto* auth_mgr = api::get_auth_manager();
    esp_err_t err = auth_mgr ? auth_mgr->rotate_credential(role, token_item->valuestring) : ESP_ERR_INVALID_STATE;
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "credential_rotation_failed");
    }

    api::AuditLog::instance().log(api::ApiRole::Admin, "auth_key_rotated", role_item->valuestring);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "role", role_item->valuestring);
    cJSON_AddBoolToObject(resp, "updated", true);
    return send_json(request, resp, "200 OK");
}

esp_err_t delete_auth_credentials(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    char query[64] = {};
    char role_str[32] = {};
    if (httpd_req_get_url_query_len(request) > 0 && httpd_req_get_url_query_len(request) < sizeof(query)) {
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
            httpd_query_key_value(query, "role", role_str, sizeof(role_str));
        }
    }

    if (role_str[0] == '\0') {
        cJSON* root = nullptr;
        if (read_json(request, &root) == ESP_OK && root) {
            cJSON* r = cJSON_GetObjectItem(root, "role");
            if (r && cJSON_IsString(r)) {
                strncpy(role_str, r->valuestring, sizeof(role_str) - 1);
            }
            cJSON_Delete(root);
        }
    }

    lg::auth::AuthRole role = lg::auth::AuthRole::Operator;
    if (strcmp(role_str, "installer") == 0) {
        role = lg::auth::AuthRole::Installer;
    } else if (strcmp(role_str, "operator") == 0) {
        role = lg::auth::AuthRole::Operator;
    } else if (strcmp(role_str, "admin") == 0) {
        return send_error(request, "400 Bad Request", "cannot_disable_admin");
    } else {
        return send_error(request, "400 Bad Request", "invalid_role");
    }

    auto* auth_mgr = api::get_auth_manager();
    esp_err_t err = auth_mgr ? auth_mgr->disable_credential(role) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "disable_credential_failed");
    }

    api::AuditLog::instance().log(api::ApiRole::Admin, "auth_role_disabled", role_str);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "role", role_str);
    cJSON_AddBoolToObject(resp, "disabled", true);
    return send_json(request, resp, "200 OK");
}

// ═══════════════════════════════════════════════════════════════════
// Signage REST Endpoints
// ═══════════════════════════════════════════════════════════════════

cJSON* build_scene_json(uint8_t i, const SceneConfig& scene) {
    cJSON* obj = cJSON_CreateObject();
    if (!obj) return nullptr;
    cJSON_AddNumberToObject(obj, "index", i);
    cJSON_AddStringToObject(obj, "name", scene.name);
    cJSON_AddNumberToObject(obj, "globalBrightness", scene.global_brightness);
    cJSON_AddNumberToObject(obj, "transitionMs", scene.transition_ms);

    cJSON* channels = cJSON_CreateArray();
    for (uint8_t c = 0; c < 16; ++c) {
        cJSON* ch_obj = cJSON_CreateObject();
        cJSON* layers = cJSON_CreateArray();
        for (uint8_t l = 0; l < 3; ++l) {
            const auto& layer = scene.channels[c].layers[l];
            cJSON* layer_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(layer_obj, "effect", signage_effect_name(static_cast<SignageEffect>(layer.effect)));
            cJSON_AddStringToObject(layer_obj, "blendMode", blend_mode_name(layer.blend_mode));
            
            cJSON* pcolor = cJSON_CreateObject();
            cJSON_AddNumberToObject(pcolor, "r", layer.primary_color.r);
            cJSON_AddNumberToObject(pcolor, "g", layer.primary_color.g);
            cJSON_AddNumberToObject(pcolor, "b", layer.primary_color.b);
            cJSON_AddItemToObject(layer_obj, "primaryColor", pcolor);
            
            cJSON* scolor = cJSON_CreateObject();
            cJSON_AddNumberToObject(scolor, "r", layer.secondary_color.r);
            cJSON_AddNumberToObject(scolor, "g", layer.secondary_color.g);
            cJSON_AddNumberToObject(scolor, "b", layer.secondary_color.b);
            cJSON_AddItemToObject(layer_obj, "secondaryColor", scolor);

            cJSON_AddNumberToObject(layer_obj, "speedMs", layer.speed_ms);
            cJSON_AddNumberToObject(layer_obj, "fxParam", layer.fx_param);
            cJSON_AddNumberToObject(layer_obj, "paletteId", layer.palette_id);
            cJSON_AddNumberToObject(layer_obj, "opacity", layer.opacity);
            cJSON_AddBoolToObject(layer_obj, "enabled", layer.enabled);
            cJSON_AddItemToArray(layers, layer_obj);
        }
        cJSON_AddItemToObject(ch_obj, "layers", layers);
        cJSON_AddItemToArray(channels, ch_obj);
    }
    cJSON_AddItemToObject(obj, "channels", channels);

    // Legacy compatibility: mirror first 8 channels into zones array
    cJSON* zones = cJSON_CreateArray();
    for (uint8_t z = 0; z < 8; ++z) {
        cJSON* zone_obj = cJSON_CreateObject();
        cJSON* layers = cJSON_CreateArray();
        for (uint8_t l = 0; l < 3; ++l) {
            const auto& layer = scene.channels[z].layers[l];
            cJSON* layer_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(layer_obj, "effect", signage_effect_name(static_cast<SignageEffect>(layer.effect)));
            cJSON_AddStringToObject(layer_obj, "blendMode", blend_mode_name(layer.blend_mode));
            cJSON_AddBoolToObject(layer_obj, "enabled", layer.enabled);
            cJSON_AddItemToArray(layers, layer_obj);
        }
        cJSON_AddItemToObject(zone_obj, "layers", layers);
        cJSON_AddItemToArray(zones, zone_obj);
    }
    cJSON_AddItemToObject(obj, "zones", zones);
    return obj;
}

esp_err_t get_presets(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    // Support singular preset fetch via ?index=N (Phase 10)
    size_t qlen = httpd_req_get_url_query_len(request);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
            char param[16];
            if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                int idx = atoi(param);
                if (idx < 0 || idx >= kMaxPresets) {
                    return send_error(request, "400 Bad Request", "invalid_index");
                }
                SceneConfig scene;
                if (api_ptr->config()->load_scene(static_cast<uint8_t>(idx), scene) != ESP_OK) {
                    return send_error(request, "404 Not Found", "scene_not_found");
                }
                cJSON* obj = build_scene_json(static_cast<uint8_t>(idx), scene);
                if (!obj) return send_error(request, "500 Internal Server Error", "json_encode_failed");
                char* json = cJSON_PrintUnformatted(obj);
                cJSON_Delete(obj);
                if (!json) return send_error(request, "500 Internal Server Error", "json_encode_failed");
                esp_err_t ret = send_json(request, json);
                cJSON_free(json);
                return ret;
            }
        }
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_send_chunk(request, "[", 1);

    bool first = true;
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneConfig scene;
        if (api_ptr->config()->load_scene(i, scene) != ESP_OK) continue;

        cJSON* obj = build_scene_json(i, scene);
        if (!obj) continue;

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

esp_err_t delete_preset(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    int index = -1;
    size_t qlen = httpd_req_get_url_query_len(request);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
            char param[16];
            if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                index = atoi(param);
            }
        }
    }

    if (index < 0 && request->content_len > 0) {
        cJSON* root = nullptr;
        if (read_json(request, &root) == ESP_OK && root) {
            uint8_t idx_u8 = 0;
            if (json_u8(root, "index", 0, kMaxPresets - 1, idx_u8)) {
                index = idx_u8;
            }
            cJSON_Delete(root);
        }
    }

    if (index < 0 || index >= kMaxPresets) {
        return send_error(request, "400 Bad Request", "invalid_or_missing_index");
    }

    api_ptr->config()->delete_scene(static_cast<uint8_t>(index));
    api_ptr->config()->delete_scene_motion(static_cast<uint8_t>(index));

    char audit_detail[64];
    snprintf(audit_detail, sizeof(audit_detail), "Preset %d deleted", index);
    api::AuditLog::instance().log(api::ApiRole::Operator, "preset_delete", audit_detail);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"deleted\":true,\"index\":%d}", index);
    return send_json(request, resp, "200 OK");
}

esp_err_t delete_scene_motion(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    int index = -1;
    char query[32];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
            index = atoi(param);
        }
    }
    if (index < 0 || index >= kMaxPresets) {
        return send_error(request, "400 Bad Request", "invalid_or_missing_index");
    }

    esp_err_t err = api_ptr->config()->delete_scene_motion(static_cast<uint8_t>(index));
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "motion_delete_failed");
    }

    char audit_detail[64];
    snprintf(audit_detail, sizeof(audit_detail), "Scene motion %d deleted", index);
    api::AuditLog::instance().log(api::ApiRole::Operator, "scene_motion_deleted", audit_detail);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"deleted\":true,\"index\":%d}", index);
    return send_json(request, resp, "200 OK");
}

esp_err_t get_show_manifest(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    NodeIdentity id = get_node_identity(nullptr);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", 2);
    cJSON_AddNumberToObject(root, "schemaVersion", 2);
    cJSON_AddStringToObject(root, "firmware", id.fw_version);
    cJSON_AddNumberToObject(root, "protocol", 1);
    cJSON_AddNumberToObject(root, "syncProtocol", 4);
    cJSON_AddNumberToObject(root, "playbackHash", api_ptr->config()->compute_playback_hash());
    cJSON_AddNumberToObject(root, "fullHash", api_ptr->config()->compute_full_show_hash());
    cJSON_AddNumberToObject(root, "scheduleHash", api_ptr->config()->compute_schedule_hash());
    cJSON_AddNumberToObject(root, "showHash", api_ptr->config()->compute_show_hash());

    // Occupied scenes
    cJSON* scenes_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneConfig scene;
        if (api_ptr->config()->load_scene(i, scene) == ESP_OK) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "index", i);
            cJSON_AddStringToObject(item, "name", scene.name);
            cJSON_AddNumberToObject(item, "checksum", scene.checksum);
            SceneMotionConfig motion;
            uint32_t motion_cs = 0;
            if (api_ptr->config()->load_scene_motion(i, motion) == ESP_OK) {
                motion_cs = motion.checksum;
            }
            cJSON_AddNumberToObject(item, "motionChecksum", motion_cs);
            cJSON_AddItemToArray(scenes_arr, item);
        }
    }
    cJSON_AddItemToObject(root, "scenes", scenes_arr);

    // Occupied scene motions
    cJSON* motions_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneMotionConfig motion;
        if (api_ptr->config()->load_scene_motion(i, motion) == ESP_OK && motion.track_count > 0) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "index", i);
            cJSON_AddNumberToObject(item, "checksum", motion.checksum);
            cJSON_AddItemToArray(motions_arr, item);
        }
    }
    cJSON_AddItemToObject(root, "sceneMotions", motions_arr);

    // Timeline sequence
    Sequence seq;
    cJSON* tl_obj = cJSON_CreateObject();
    if (api_ptr->config()->load_sequence(seq) == ESP_OK) {
        cJSON_AddBoolToObject(tl_obj, "enabled", seq.enabled);
        cJSON_AddNumberToObject(tl_obj, "bpm", seq.bpm);
        cJSON_AddNumberToObject(tl_obj, "checksum", seq.checksum);
    } else {
        cJSON_AddBoolToObject(tl_obj, "enabled", false);
        cJSON_AddNumberToObject(tl_obj, "bpm", 120);
        cJSON_AddNumberToObject(tl_obj, "checksum", 0);
    }
    cJSON_AddItemToObject(root, "timeline", tl_obj);

    // Calendar / Schedules
    CalendarSchedule sched;
    cJSON* cal_obj = cJSON_CreateObject();
    if (api_ptr->config()->load_calendar_schedule(sched) == ESP_OK) {
        cJSON_AddNumberToObject(cal_obj, "checksum", sched.checksum);
    } else {
        cJSON_AddNumberToObject(cal_obj, "checksum", 0);
    }
    cJSON_AddItemToObject(root, "calendar", cal_obj);
    cJSON* sched_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(sched_obj, "checksum", cJSON_GetObjectItem(cal_obj, "checksum")->valueint);
    cJSON_AddItemToObject(root, "schedules", sched_obj);

    // Cue lists
    cJSON* cl_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < kMaxCueLists; ++i) {
        CueList cl;
        if (api_ptr->config()->load_cue_list(i, cl) == ESP_OK && cl.count > 0) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "index", i);
            cJSON_AddStringToObject(item, "name", cl.name);
            cJSON_AddNumberToObject(item, "cueCount", cl.count);
            cJSON_AddNumberToObject(item, "checksum", cl.checksum);
            cJSON_AddItemToArray(cl_arr, item);
        }
    }
    cJSON_AddItemToObject(root, "cueLists", cl_arr);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return send_error(request, "500 Internal Server Error", "json_encode_failed");

    esp_err_t ret = send_json(request, json);
    cJSON_free(json);
    return ret;
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

    auto parse_layer_obj = [](const cJSON* l_item, LayerConfig& layer) {
        const cJSON* effect_j = cJSON_GetObjectItemCaseSensitive(l_item, "effect");
        if (effect_j && cJSON_IsString(effect_j)) {
            SignageEffect eff = SignageEffect::Solid;
            if (parse_signage_effect(effect_j->valuestring, eff)) {
                layer.effect = static_cast<uint8_t>(eff);
            }
        } else if (effect_j && cJSON_IsNumber(effect_j)) {
            layer.effect = static_cast<uint8_t>(effect_j->valueint);
        }

        const cJSON* blend_j = cJSON_GetObjectItemCaseSensitive(l_item, "blendMode");
        if (blend_j && cJSON_IsString(blend_j)) {
            parse_blend_mode(blend_j->valuestring, layer.blend_mode);
        } else if (blend_j && cJSON_IsNumber(blend_j)) {
            layer.blend_mode = static_cast<BlendMode>(blend_j->valueint);
        }

        const cJSON* pc = cJSON_GetObjectItemCaseSensitive(l_item, "primaryColor");
        if (pc) {
            const cJSON* r = cJSON_GetObjectItemCaseSensitive(pc, "r");
            const cJSON* g = cJSON_GetObjectItemCaseSensitive(pc, "g");
            const cJSON* b = cJSON_GetObjectItemCaseSensitive(pc, "b");
            if (r && cJSON_IsNumber(r)) layer.primary_color.r = static_cast<uint8_t>(r->valueint);
            if (g && cJSON_IsNumber(g)) layer.primary_color.g = static_cast<uint8_t>(g->valueint);
            if (b && cJSON_IsNumber(b)) layer.primary_color.b = static_cast<uint8_t>(b->valueint);
        }

        const cJSON* sc = cJSON_GetObjectItemCaseSensitive(l_item, "secondaryColor");
        if (sc) {
            const cJSON* r = cJSON_GetObjectItemCaseSensitive(sc, "r");
            const cJSON* g = cJSON_GetObjectItemCaseSensitive(sc, "g");
            const cJSON* b = cJSON_GetObjectItemCaseSensitive(sc, "b");
            if (r && cJSON_IsNumber(r)) layer.secondary_color.r = static_cast<uint8_t>(r->valueint);
            if (g && cJSON_IsNumber(g)) layer.secondary_color.g = static_cast<uint8_t>(g->valueint);
            if (b && cJSON_IsNumber(b)) layer.secondary_color.b = static_cast<uint8_t>(b->valueint);
        }

        const cJSON* spd = cJSON_GetObjectItemCaseSensitive(l_item, "speedMs");
        if (spd && cJSON_IsNumber(spd)) layer.speed_ms = static_cast<uint16_t>(spd->valueint);

        const cJSON* fx = cJSON_GetObjectItemCaseSensitive(l_item, "fxParam");
        if (fx && cJSON_IsNumber(fx)) layer.fx_param = static_cast<uint8_t>(fx->valueint);

        const cJSON* pal = cJSON_GetObjectItemCaseSensitive(l_item, "paletteId");
        if (pal && cJSON_IsNumber(pal)) layer.palette_id = static_cast<uint8_t>(pal->valueint);

        const cJSON* op = cJSON_GetObjectItemCaseSensitive(l_item, "opacity");
        if (op && cJSON_IsNumber(op)) layer.opacity = static_cast<uint8_t>(op->valueint);

        const cJSON* en = cJSON_GetObjectItemCaseSensitive(l_item, "enabled");
        if (en && cJSON_IsBool(en)) layer.enabled = cJSON_IsTrue(en);
    };

    const cJSON* channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (channels && cJSON_IsArray(channels)) {
        int c_idx = 0;
        cJSON* ch_item = nullptr;
        cJSON_ArrayForEach(ch_item, channels) {
            if (c_idx >= 16) break;
            const cJSON* layers = cJSON_GetObjectItemCaseSensitive(ch_item, "layers");
            if (layers && cJSON_IsArray(layers)) {
                int l_idx = 0;
                cJSON* l_item = nullptr;
                cJSON_ArrayForEach(l_item, layers) {
                    if (l_idx >= 3) break;
                    parse_layer_obj(l_item, scene.channels[c_idx].layers[l_idx]);
                    l_idx++;
                }
            }
            c_idx++;
        }
    } else {
        const cJSON* zones = cJSON_GetObjectItemCaseSensitive(root, "zones");
        if (zones && cJSON_IsArray(zones)) {
            int z_idx = 0;
            cJSON* z_item = nullptr;
            cJSON_ArrayForEach(z_item, zones) {
                if (z_idx >= 8) break;
                const cJSON* layers = cJSON_GetObjectItemCaseSensitive(z_item, "layers");
                if (layers && cJSON_IsArray(layers)) {
                    int l_idx = 0;
                    cJSON* l_item = nullptr;
                    cJSON_ArrayForEach(l_item, layers) {
                        if (l_idx >= 3) break;
                        parse_layer_obj(l_item, scene.channels[z_idx].layers[l_idx]);
                        l_idx++;
                    }
                }
                z_idx++;
            }
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

esp_err_t get_scene_motion(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    uint8_t target_index = 255;
    size_t qlen = httpd_req_get_url_query_len(request);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
            char param[16];
            if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                target_index = static_cast<uint8_t>(atoi(param));
            }
        }
    }

    auto serialize_motion = [](uint8_t idx, const SceneMotionConfig& sm) -> cJSON* {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "schema_version", sm.schema_version);
        cJSON_AddNumberToObject(obj, "index", idx);
        cJSON_AddStringToObject(obj, "transitionCurve", transition_curve_name(sm.transition_curve));
        cJSON_AddNumberToObject(obj, "trackCount", sm.track_count);
        cJSON* tracks = cJSON_CreateArray();
        for (uint8_t t = 0; t < sm.track_count && t < kAutomationTracksPerScene; ++t) {
            const auto& tr = sm.tracks[t];
            cJSON* tobj = cJSON_CreateObject();
            cJSON_AddStringToObject(tobj, "target", automation_target_name(tr.target));
            cJSON_AddNumberToObject(tobj, "channel", tr.channel);
            // Deprecated compatibility alias for clients/backups created before
            // the 16-channel UI model. New clients must use "channel".
            cJSON_AddNumberToObject(tobj, "zone", tr.channel);
            cJSON_AddNumberToObject(tobj, "layer", tr.layer);
            cJSON_AddStringToObject(tobj, "curve", automation_curve_name(tr.curve));
            cJSON_AddBoolToObject(tobj, "loop", tr.loop);
            cJSON_AddNumberToObject(tobj, "periodMs", tr.period_ms);
            cJSON* pts = cJSON_CreateArray();
            for (uint8_t p = 0; p < tr.point_count && p < kAutomationPointsPerTrack; ++p) {
                cJSON* pobj = cJSON_CreateObject();
                cJSON_AddNumberToObject(pobj, "position", tr.points[p].position);
                cJSON_AddNumberToObject(pobj, "value", tr.points[p].value);
                cJSON_AddItemToArray(pts, pobj);
            }
            cJSON_AddItemToObject(tobj, "points", pts);
            cJSON_AddItemToArray(tracks, tobj);
        }
        cJSON_AddItemToObject(obj, "tracks", tracks);
        return obj;
    };

    if (target_index < kMaxPresets) {
        SceneMotionConfig sm{};
        (void)api_ptr->config()->load_scene_motion(target_index, sm);
        cJSON* obj = serialize_motion(target_index, sm);
        char* json = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (!json) return send_error(request, "500 Internal Server Error", "out_of_memory");
        esp_err_t res = send_json(request, json);
        cJSON_free(json);
        return res;
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_send_chunk(request, "[", 1);
    bool first = true;
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        SceneMotionConfig sm{};
        if (api_ptr->config()->load_scene_motion(i, sm) != ESP_OK && sm.track_count == 0 && sm.transition_curve == TransitionCurve::EaseInOut) {
            continue;
        }
        cJSON* obj = serialize_motion(i, sm);
        if (!obj) continue;
        char* json = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
        if (json) {
            if (!first) httpd_resp_send_chunk(request, ",", 1);
            first = false;
            httpd_resp_send_chunk(request, json, strlen(json));
            cJSON_free(json);
        }
    }
    httpd_resp_send_chunk(request, "]", 1);
    httpd_resp_send_chunk(request, nullptr, 0);
    return ESP_OK;
}

esp_err_t put_scene_motion(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) return send_error(request, "400 Bad Request", "invalid_json");

    uint8_t index = 0;
    if (!json_u8(root, "index", 0, kMaxPresets - 1, index)) {
        size_t qlen = httpd_req_get_url_query_len(request);
        if (qlen > 0 && qlen < 64) {
            char query[64];
            if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
                char param[16];
                if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                    index = static_cast<uint8_t>(atoi(param));
                }
            }
        }
    }
    if (index >= kMaxPresets) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_index");
    }

    SceneMotionConfig motion{};
    motion.schema_version = 1;

    const cJSON* cur_j = cJSON_GetObjectItemCaseSensitive(root, "transitionCurve");
    if (cur_j && cJSON_IsString(cur_j)) {
        parse_transition_curve(cur_j->valuestring, motion.transition_curve);
    }

    const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (tracks && cJSON_IsArray(tracks)) {
        uint8_t t_count = 0;
        const cJSON* t_item = nullptr;
        cJSON_ArrayForEach(t_item, tracks) {
            if (t_count >= kAutomationTracksPerScene) break;
            auto& tr = motion.tracks[t_count];

            const cJSON* tgt_j = cJSON_GetObjectItemCaseSensitive(t_item, "target");
            if (tgt_j && cJSON_IsString(tgt_j)) parse_automation_target(tgt_j->valuestring, tr.target);

            const cJSON* ch_j = cJSON_GetObjectItemCaseSensitive(t_item, "channel");
            const cJSON* z_j = cJSON_GetObjectItemCaseSensitive(t_item, "zone");
            if (ch_j && cJSON_IsNumber(ch_j)) {
                tr.channel = std::min<uint8_t>(15, std::max<int>(0, ch_j->valueint));
            } else if (z_j && cJSON_IsNumber(z_j)) {
                // Legacy 8-zone payload fallback.
                tr.channel = std::min<uint8_t>(7, std::max<int>(0, z_j->valueint));
            }

            const cJSON* l_j = cJSON_GetObjectItemCaseSensitive(t_item, "layer");
            if (l_j && cJSON_IsNumber(l_j)) tr.layer = std::min<uint8_t>(2, std::max<int>(0, l_j->valueint));

            const cJSON* c_j = cJSON_GetObjectItemCaseSensitive(t_item, "curve");
            if (c_j && cJSON_IsString(c_j)) parse_automation_curve(c_j->valuestring, tr.curve);

            const cJSON* loop_j = cJSON_GetObjectItemCaseSensitive(t_item, "loop");
            if (loop_j && cJSON_IsBool(loop_j)) tr.loop = cJSON_IsTrue(loop_j);

            const cJSON* per_j = cJSON_GetObjectItemCaseSensitive(t_item, "periodMs");
            if (per_j && cJSON_IsNumber(per_j)) tr.period_ms = std::max<uint32_t>(100, std::min<uint32_t>(3600000, per_j->valueint));

            const cJSON* pts = cJSON_GetObjectItemCaseSensitive(t_item, "points");
            if (pts && cJSON_IsArray(pts)) {
                uint8_t p_count = 0;
                const cJSON* p_item = nullptr;
                cJSON_ArrayForEach(p_item, pts) {
                    if (p_count >= kAutomationPointsPerTrack) break;
                    const cJSON* pos_j = cJSON_GetObjectItemCaseSensitive(p_item, "position");
                    const cJSON* val_j = cJSON_GetObjectItemCaseSensitive(p_item, "value");
                    if (pos_j && val_j && cJSON_IsNumber(pos_j) && cJSON_IsNumber(val_j)) {
                        tr.points[p_count].position = static_cast<uint16_t>(std::max<int>(0, std::min<int>(65535, pos_j->valueint)));
                        tr.points[p_count].value = static_cast<uint16_t>(std::max<int>(0, std::min<int>(65535, val_j->valueint)));
                        p_count++;
                    }
                }
                tr.point_count = p_count;
            }
            t_count++;
        }
        motion.track_count = t_count;
    }

    cJSON_Delete(root);

    esp_err_t err = api_ptr->config()->save_scene_motion(index, motion);
    if (err != ESP_OK) return send_error(request, "500 Internal Server Error", "nvs_write_failed");

    if (api_ptr->signage()) {
        api_ptr->signage()->scene_manager().reload_motion(index);
    }

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::SceneMotion);
    api_ptr->command_journal().applied(cmd_id);

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"saved\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "200 OK");
}

esp_err_t get_cue_lists(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < kMaxCueLists; ++i) {
        CueList cl;
        if (api_ptr->config()->load_cue_list(i, cl) == ESP_OK && cl.count > 0) {
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "index", i);
            cJSON_AddStringToObject(obj, "name", cl.name);
            cJSON_AddStringToObject(obj, "mode", cue_run_mode_name(cl.mode));
            cJSON_AddNumberToObject(obj, "count", cl.count);
            cJSON_AddNumberToObject(obj, "endAction", cl.end_action);
            cJSON_AddItemToArray(arr, obj);
        }
    }
    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) return send_error(request, "500 Internal Server Error", "out_of_memory");
    esp_err_t res = send_json(request, json);
    cJSON_free(json);
    return res;
}

esp_err_t get_cue_list(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    uint8_t index = 0;
    size_t qlen = httpd_req_get_url_query_len(request);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
            char param[16];
            if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                index = static_cast<uint8_t>(atoi(param));
            }
        }
    }
    if (index >= kMaxCueLists) return send_error(request, "400 Bad Request", "invalid_index");

    CueList cl;
    esp_err_t err = api_ptr->config()->load_cue_list(index, cl);
    if (err != ESP_OK) return send_error(request, "404 Not Found", "cue_list_not_found");

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "index", index);
    cJSON_AddStringToObject(obj, "name", cl.name);
    cJSON_AddStringToObject(obj, "mode", cue_run_mode_name(cl.mode));
    cJSON_AddNumberToObject(obj, "count", cl.count);
    cJSON_AddNumberToObject(obj, "endAction", cl.end_action);

    cJSON* cues = cJSON_CreateArray();
    for (uint8_t c = 0; c < cl.count && c < kMaxCuesPerList; ++c) {
        cJSON* cobj = cJSON_CreateObject();
        cJSON_AddNumberToObject(cobj, "sceneIndex", cl.cues[c].scene_index);
        cJSON_AddNumberToObject(cobj, "holdMs", cl.cues[c].hold_ms);
        cJSON_AddNumberToObject(cobj, "fadeMs", cl.cues[c].fade_ms);
        cJSON_AddStringToObject(cobj, "curve", transition_curve_name(cl.cues[c].curve));
        cJSON_AddStringToObject(cobj, "label", cl.cues[c].label);
        cJSON_AddItemToArray(cues, cobj);
    }
    cJSON_AddItemToObject(obj, "cues", cues);

    char* json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return send_error(request, "500 Internal Server Error", "out_of_memory");
    esp_err_t res = send_json(request, json);
    cJSON_free(json);
    return res;
}

esp_err_t put_cue_list(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) return send_error(request, "400 Bad Request", "invalid_json");

    uint8_t index = 0;
    if (!json_u8(root, "index", 0, kMaxCueLists - 1, index)) {
        size_t qlen = httpd_req_get_url_query_len(request);
        if (qlen > 0 && qlen < 64) {
            char query[64];
            if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
                char param[16];
                if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                    index = static_cast<uint8_t>(atoi(param));
                }
            }
        }
    }
    if (index >= kMaxCueLists) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_index");
    }

    CueList cl{};
    const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (name_j && cJSON_IsString(name_j)) {
        strncpy(cl.name, name_j->valuestring, sizeof(cl.name) - 1);
    }
    const cJSON* mode_j = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (mode_j && cJSON_IsString(mode_j)) {
        parse_cue_run_mode(mode_j->valuestring, cl.mode);
    }
    const cJSON* ea_j = cJSON_GetObjectItemCaseSensitive(root, "endAction");
    if (ea_j && cJSON_IsNumber(ea_j)) cl.end_action = ea_j->valueint;

    const cJSON* cues = cJSON_GetObjectItemCaseSensitive(root, "cues");
    if (cues && cJSON_IsArray(cues)) {
        uint8_t c_idx = 0;
        const cJSON* c_item = nullptr;
        cJSON_ArrayForEach(c_item, cues) {
            if (c_idx >= kMaxCuesPerList) break;
            auto& cue = cl.cues[c_idx];

            const cJSON* sc_j = cJSON_GetObjectItemCaseSensitive(c_item, "sceneIndex");
            if (sc_j && cJSON_IsNumber(sc_j)) cue.scene_index = sc_j->valueint;

            const cJSON* hold_j = cJSON_GetObjectItemCaseSensitive(c_item, "holdMs");
            if (hold_j && cJSON_IsNumber(hold_j)) cue.hold_ms = hold_j->valueint;

            const cJSON* fade_j = cJSON_GetObjectItemCaseSensitive(c_item, "fadeMs");
            if (fade_j && cJSON_IsNumber(fade_j)) cue.fade_ms = fade_j->valueint;

            const cJSON* cur_j = cJSON_GetObjectItemCaseSensitive(c_item, "curve");
            if (cur_j && cJSON_IsString(cur_j)) parse_transition_curve(cur_j->valuestring, cue.curve);

            const cJSON* lbl_j = cJSON_GetObjectItemCaseSensitive(c_item, "label");
            if (lbl_j && cJSON_IsString(lbl_j)) strncpy(cue.label, lbl_j->valuestring, sizeof(cue.label) - 1);

            c_idx++;
        }
        cl.count = c_idx;
    }

    cJSON_Delete(root);

    esp_err_t err = api_ptr->config()->save_cue_list(index, cl);
    if (err != ESP_OK) return send_error(request, "500 Internal Server Error", "nvs_write_failed");

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Cue);
    api_ptr->command_journal().applied(cmd_id);

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"saved\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "200 OK");
}

esp_err_t delete_cue_list(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    uint8_t index = 0;
    size_t qlen = httpd_req_get_url_query_len(request);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
            char param[16];
            if (httpd_query_key_value(query, "index", param, sizeof(param)) == ESP_OK) {
                index = static_cast<uint8_t>(atoi(param));
            }
        }
    }
    if (index >= kMaxCueLists) return send_error(request, "400 Bad Request", "invalid_index");

    esp_err_t err = api_ptr->config()->delete_cue_list(index);
    if (err != ESP_OK) return send_error(request, "500 Internal Server Error", "delete_failed");

    return send_json(request, "{\"deleted\":true}", "200 OK");
}

esp_err_t post_show_control(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->signage()) return send_error(request, "503 Service Unavailable", "signage_not_ready");

    char leader_ip[16] = {};
    if (is_sync_follower_read_only(api_ptr, leader_ip, sizeof(leader_ip))) {
        char err_body[96];
        snprintf(err_body, sizeof(err_body), "{\"error\":\"sync_follower_read_only\",\"leader\":\"%s\"}", leader_ip);
        return send_json(request, err_body, "409 Conflict");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) return send_error(request, "400 Bad Request", "invalid_json");

    const cJSON* mode_j = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (!mode_j || !cJSON_IsString(mode_j)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_mode");
    }

    const char* mode_str = mode_j->valuestring;
    uint64_t now_ms = api_ptr->show_clock_now_ms();
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Scene);

    if (strcmp(mode_str, "scene") == 0) {
        int scene_idx = 0;
        const cJSON* si_j = cJSON_GetObjectItemCaseSensitive(root, "sceneIndex");
        if (!si_j) si_j = cJSON_GetObjectItemCaseSensitive(root, "index");
        if (!si_j) si_j = cJSON_GetObjectItemCaseSensitive(root, "presetIndex");
        if (si_j && cJSON_IsNumber(si_j)) {
            scene_idx = si_j->valueint;
        }
        if (scene_idx < 0 || scene_idx >= kMaxPresets) {
            cJSON_Delete(root);
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
            return send_error(request, "400 Bad Request", "invalid_scene_index");
        }

        if (api_ptr->cue_runner()) {
            api_ptr->cue_runner()->stop(now_ms);
        }
        if (api_ptr->scheduler()) {
            api_ptr->scheduler()->stop_local_timeline();
            api_ptr->scheduler()->clear_sync_transport();
        }

        uint32_t gen = 0;
        if (!api_ptr->signage()->activate_scene_checked(static_cast<uint8_t>(scene_idx), &gen)) {
            cJSON_Delete(root);
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
            return send_error(request, "404 Not Found", "scene_not_found");
        }

        api_ptr->signage()->feed_watchdog();
        api_ptr->signage()->release_control(ControlSource::EStop);

        uint32_t transport_gen = gen;
        if (api_ptr->show_transport()) {
            api_ptr->show_transport()->set_scene(static_cast<uint8_t>(scene_idx));
            transport_gen = api_ptr->show_transport()->generation();
        }

        cJSON_Delete(root);
        api_ptr->command_journal().applied(cmd_id);
        api_ptr->notify_status_async();

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"accepted\":true,\"mode\":\"scene\",\"generation\":%" PRIu32 ",\"commandId\":%" PRIu32 "}", transport_gen, cmd_id);
        return send_json(request, resp, "200 OK");
    } else if (strcmp(mode_str, "timeline") == 0) {
        const cJSON* act_j = cJSON_GetObjectItemCaseSensitive(root, "action");
        const char* action = (act_j && cJSON_IsString(act_j)) ? act_j->valuestring : "start";

        uint32_t transport_gen = 0;
        if (strcmp(action, "start") == 0) {
            if (api_ptr->cue_runner()) {
                api_ptr->cue_runner()->stop(now_ms);
            }
            uint16_t bpm = 120;
            Sequence seq{};
            if (api_ptr->config() && api_ptr->config()->load_sequence(seq) == ESP_OK) {
                bpm = seq.bpm > 0 ? seq.bpm : 120;
            }
            const cJSON* bpm_j = cJSON_GetObjectItemCaseSensitive(root, "bpm");
            if (bpm_j && cJSON_IsNumber(bpm_j) && bpm_j->valueint > 0) {
                bpm = static_cast<uint16_t>(bpm_j->valueint);
            }
            if (api_ptr->scheduler()) {
                api_ptr->scheduler()->start_local_timeline(bpm);
            }
            if (api_ptr->show_transport()) {
                api_ptr->show_transport()->set_timeline(true, bpm);
                transport_gen = api_ptr->show_transport()->generation();
            }
        } else if (strcmp(action, "stop") == 0) {
            if (api_ptr->scheduler()) {
                api_ptr->scheduler()->stop_local_timeline();
            }
            if (api_ptr->show_transport()) {
                api_ptr->show_transport()->set_timeline(false, 120);
                transport_gen = api_ptr->show_transport()->generation();
            }
        } else {
            cJSON_Delete(root);
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
            return send_error(request, "400 Bad Request", "unknown_action");
        }

        cJSON_Delete(root);
        api_ptr->command_journal().applied(cmd_id);
        api_ptr->notify_status_async();

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"accepted\":true,\"mode\":\"timeline\",\"generation\":%" PRIu32 ",\"commandId\":%" PRIu32 "}", transport_gen, cmd_id);
        return send_json(request, resp, "200 OK");
    } else if (strcmp(mode_str, "cue") == 0) {
        if (!api_ptr->cue_runner()) {
            cJSON_Delete(root);
            return send_error(request, "503 Service Unavailable", "cue_runner_not_ready");
        }

        const cJSON* act_j = cJSON_GetObjectItemCaseSensitive(root, "action");
        if (!act_j || !cJSON_IsString(act_j)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "missing_action");
        }
        const char* action = act_j->valuestring;

        if (strcmp(action, "start") == 0) {
            if (api_ptr->scheduler()) {
                api_ptr->scheduler()->stop_local_timeline();
            }
            uint8_t list_idx = 0;
            uint8_t start_cue = 0;
            const cJSON* li = cJSON_GetObjectItemCaseSensitive(root, "cueListIndex");
            if (li && cJSON_IsNumber(li)) list_idx = static_cast<uint8_t>(li->valueint);
            const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
            if (ci && cJSON_IsNumber(ci)) start_cue = static_cast<uint8_t>(ci->valueint);
            esp_err_t err = api_ptr->cue_runner()->start(list_idx, start_cue, now_ms);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "start_failed");
            }
        } else if (strcmp(action, "stop") == 0) {
            api_ptr->cue_runner()->stop(now_ms);
        } else if (strcmp(action, "pause") == 0) {
            api_ptr->cue_runner()->pause();
        } else if (strcmp(action, "resume") == 0) {
            api_ptr->cue_runner()->resume(now_ms);
        } else if (strcmp(action, "next") == 0) {
            api_ptr->cue_runner()->next(now_ms);
        } else if (strcmp(action, "previous") == 0) {
            api_ptr->cue_runner()->previous(now_ms);
        } else if (strcmp(action, "jump") == 0) {
            uint8_t cue_idx = 0;
            const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
            if (ci && cJSON_IsNumber(ci)) cue_idx = static_cast<uint8_t>(ci->valueint);
            api_ptr->cue_runner()->jump(cue_idx, now_ms);
        } else if (strcmp(action, "crossfade") == 0) {
            uint16_t fade_q15 = 0;
            const cJSON* f_j = cJSON_GetObjectItemCaseSensitive(root, "crossfade");
            if (f_j && cJSON_IsNumber(f_j)) {
                if (f_j->valuedouble <= 1.0) {
                    fade_q15 = static_cast<uint16_t>(f_j->valuedouble * 32768.0);
                } else {
                    fade_q15 = static_cast<uint16_t>(std::min<int>(32768, f_j->valueint));
                }
            }
            api_ptr->cue_runner()->set_manual_fade(fade_q15);
        } else {
            cJSON_Delete(root);
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
            return send_error(request, "400 Bad Request", "unknown_action");
        }

        uint32_t transport_gen = 0;
        if (api_ptr->show_transport()) {
            if (api_ptr->cue_runner()->is_running()) {
                CueTransportSnapshot cs{};
                auto cr_snap = api_ptr->cue_runner()->sync_snapshot(now_ms);
                cs.active = cr_snap.active;
                cs.list_index = cr_snap.list_index;
                cs.state = static_cast<uint8_t>(cr_snap.state);
                cs.current_cue = cr_snap.current_cue;
                cs.next_cue = cr_snap.next_cue;
                cs.manual_fade_q15 = cr_snap.manual_fade_q15;
                cs.cue_anchor_ms = cr_snap.cue_anchor_ms;
                cs.paused_elapsed_ms = cr_snap.paused_elapsed_ms;
                cs.generation = cr_snap.generation;
                api_ptr->show_transport()->set_cue(cs);
            } else {
                api_ptr->show_transport()->set_mode(ShowTransportMode::Scene);
            }
            transport_gen = api_ptr->show_transport()->generation();
        }

        cJSON_Delete(root);
        api_ptr->command_journal().applied(cmd_id);
        api_ptr->notify_status_async();

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"accepted\":true,\"mode\":\"cue\",\"generation\":%" PRIu32 ",\"commandId\":%" PRIu32 "}", transport_gen, cmd_id);
        return send_json(request, resp, "200 OK");
    }

    cJSON_Delete(root);
    api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
    return send_error(request, "400 Bad Request", "unsupported_mode");
}

esp_err_t get_show_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint64_t now_ms = api_ptr->show_clock_now_ms();

    ShowTransportSnapshot st{};
    if (api_ptr->show_transport()) {
        st = api_ptr->show_transport()->snapshot();
    } else {
        st.mode = ShowTransportMode::Scene;
        if (api_ptr->signage()) st.active_scene = api_ptr->signage()->active_scene_index();
    }

    const char* mode_str = "scene";
    if (st.mode == ShowTransportMode::Timeline) mode_str = "timeline";
    else if (st.mode == ShowTransportMode::Cue) mode_str = "cue";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", mode_str);
    cJSON_AddNumberToObject(root, "generation", st.generation);

    cJSON* scene_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(scene_obj, "index", st.active_scene);
    cJSON_AddItemToObject(root, "scene", scene_obj);

    cJSON* tl_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(tl_obj, "running", st.timeline_running());
    cJSON_AddNumberToObject(tl_obj, "bpm", st.bpm);
    cJSON_AddItemToObject(root, "timeline", tl_obj);

    cJSON* cue_obj = cJSON_CreateObject();
    if (api_ptr->cue_runner()) {
        const auto* cr = api_ptr->cue_runner();
        cJSON_AddBoolToObject(cue_obj, "running", cr->is_running());
        cJSON_AddStringToObject(cue_obj, "state", cr->state_name());
        cJSON_AddNumberToObject(cue_obj, "listIndex", cr->active_list_index());
        cJSON_AddNumberToObject(cue_obj, "cueIndex", cr->current_cue_index());
        cJSON_AddNumberToObject(cue_obj, "nextCueIndex", cr->next_cue_index());
        cJSON_AddNumberToObject(cue_obj, "elapsedMs", cr->cue_elapsed_ms(now_ms));
        cJSON_AddNumberToObject(cue_obj, "remainingMs", cr->cue_remaining_ms(now_ms));
        cJSON_AddNumberToObject(cue_obj, "manualFade", cr->manual_fade_q15());
        cJSON_AddNumberToObject(cue_obj, "generation", cr->transport_generation());
    } else {
        cJSON_AddBoolToObject(cue_obj, "running", false);
        cJSON_AddStringToObject(cue_obj, "state", "stopped");
        cJSON_AddNumberToObject(cue_obj, "listIndex", 0);
        cJSON_AddNumberToObject(cue_obj, "cueIndex", 0);
        cJSON_AddNumberToObject(cue_obj, "nextCueIndex", 0);
        cJSON_AddNumberToObject(cue_obj, "elapsedMs", 0);
        cJSON_AddNumberToObject(cue_obj, "remainingMs", 0);
        cJSON_AddNumberToObject(cue_obj, "manualFade", 0);
        cJSON_AddNumberToObject(cue_obj, "generation", 0);
    }
    cJSON_AddItemToObject(root, "cue", cue_obj);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return send_error(request, "500 Internal Server Error", "json_encode_failed");

    esp_err_t res = send_json(request, json, "200 OK");
    cJSON_free(json);
    return res;
}

esp_err_t post_cue_control(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->cue_runner()) return send_error(request, "503 Service Unavailable", "cue_runner_not_ready");

    char leader_ip[16] = {};
    if (is_sync_follower_read_only(api_ptr, leader_ip, sizeof(leader_ip))) {
        char err_body[96];
        snprintf(err_body, sizeof(err_body), "{\"error\":\"sync_follower_read_only\",\"leader\":\"%s\"}", leader_ip);
        return send_json(request, err_body, "409 Conflict");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) return send_error(request, "400 Bad Request", "invalid_json");

    const cJSON* act_j = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!act_j || !cJSON_IsString(act_j)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_action");
    }

    const char* action = act_j->valuestring;
    uint64_t now_ms = api_ptr->show_clock_now_ms();
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Cue);

    if (strcmp(action, "start") == 0) {
        if (api_ptr->scheduler()) {
            api_ptr->scheduler()->stop_local_timeline();
        }
        uint8_t list_idx = 0;
        uint8_t start_cue = 0;
        const cJSON* li = cJSON_GetObjectItemCaseSensitive(root, "cueListIndex");
        if (li && cJSON_IsNumber(li)) list_idx = li->valueint;
        const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
        if (ci && cJSON_IsNumber(ci)) start_cue = ci->valueint;
        esp_err_t err = api_ptr->cue_runner()->start(list_idx, start_cue, now_ms);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "start_failed");
        }
    } else if (strcmp(action, "stop") == 0) {
        api_ptr->cue_runner()->stop(now_ms);
    } else if (strcmp(action, "pause") == 0) {
        api_ptr->cue_runner()->pause();
    } else if (strcmp(action, "resume") == 0) {
        api_ptr->cue_runner()->resume(now_ms);
    } else if (strcmp(action, "next") == 0) {
        api_ptr->cue_runner()->next(now_ms);
    } else if (strcmp(action, "previous") == 0) {
        api_ptr->cue_runner()->previous(now_ms);
    } else if (strcmp(action, "jump") == 0) {
        uint8_t cue_idx = 0;
        const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
        if (ci && cJSON_IsNumber(ci)) cue_idx = ci->valueint;
        api_ptr->cue_runner()->jump(cue_idx, now_ms);
    } else if (strcmp(action, "crossfade") == 0) {
        uint16_t fade_q15 = 0;
        const cJSON* f_j = cJSON_GetObjectItemCaseSensitive(root, "crossfade");
        if (f_j && cJSON_IsNumber(f_j)) {
            if (f_j->valuedouble <= 1.0) {
                fade_q15 = static_cast<uint16_t>(f_j->valuedouble * 32768.0);
            } else {
                fade_q15 = static_cast<uint16_t>(std::min<int>(32768, f_j->valueint));
            }
        }
        api_ptr->cue_runner()->set_manual_fade(fade_q15);
    } else {
        cJSON_Delete(root);
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
        return send_error(request, "400 Bad Request", "unknown_action");
    }

    if (api_ptr->show_transport()) {
        if (api_ptr->cue_runner()->is_running()) {
            CueTransportSnapshot cs{};
            auto cr_snap = api_ptr->cue_runner()->sync_snapshot(now_ms);
            cs.active = cr_snap.active;
            cs.list_index = cr_snap.list_index;
            cs.state = static_cast<uint8_t>(cr_snap.state);
            cs.current_cue = cr_snap.current_cue;
            cs.next_cue = cr_snap.next_cue;
            cs.manual_fade_q15 = cr_snap.manual_fade_q15;
            cs.cue_anchor_ms = cr_snap.cue_anchor_ms;
            cs.paused_elapsed_ms = cr_snap.paused_elapsed_ms;
            cs.generation = cr_snap.generation;
            api_ptr->show_transport()->set_cue(cs);
        } else {
            api_ptr->show_transport()->set_mode(ShowTransportMode::Scene);
        }
    }

    cJSON_Delete(root);
    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp, "200 OK");
}

esp_err_t get_cue_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->cue_runner()) {
        return send_json(request, "{\"running\":false}", "200 OK");
    }

    const auto* cr = api_ptr->cue_runner();
    uint64_t now_ms = api_ptr->show_clock_now_ms();
    char body[384];
    snprintf(body, sizeof(body),
        "{\"running\":%s,\"state\":\"%s\",\"listIndex\":%u,\"cueIndex\":%u,\"nextCueIndex\":%u,\"mode\":\"%s\",\"elapsedMs\":%" PRIu32 ",\"remainingMs\":%" PRIu32 ",\"manualFade\":%u,\"generation\":%" PRIu32 "}",
        cr->is_running() ? "true" : "false",
        cr->state_name(),
        cr->active_list_index(),
        cr->current_cue_index(),
        cr->next_cue_index(),
        cue_run_mode_name(cr->current_list().mode),
        cr->cue_elapsed_ms(now_ms),
        cr->cue_remaining_ms(now_ms),
        cr->manual_fade_q15(),
        cr->transport_generation());

    return send_json(request, body, "200 OK");
}

esp_err_t post_activate(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->signage()) {
        return send_error(request, "503 Service Unavailable", "signage_not_ready");
    }

    char leader_ip[16] = {};
    if (is_sync_follower_read_only(api_ptr, leader_ip, sizeof(leader_ip))) {
        char err_body[96];
        snprintf(err_body, sizeof(err_body), "{\"error\":\"sync_follower_read_only\",\"leader\":\"%s\"}", leader_ip);
        return send_json(request, err_body, "409 Conflict");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "preset", 0, kMaxPresets - 1, index) &&
        !json_u8(root, "index", 0, kMaxPresets - 1, index) &&
        !json_u8(root, "presetIndex", 0, kMaxPresets - 1, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_preset_index");
    }

    cJSON_Delete(root);

    uint64_t now_ms = api_ptr->show_clock_now_ms();

    // Stop Cue if running
    if (api_ptr->cue_runner()) {
        api_ptr->cue_runner()->stop(now_ms);
    }
    // Suppress local Timeline
    if (api_ptr->scheduler()) {
        api_ptr->scheduler()->stop_local_timeline();
        api_ptr->scheduler()->clear_sync_transport();
    }

    uint32_t generation = 0;
    if (!api_ptr->signage()->activate_scene_checked(index, &generation)) {
        const uint32_t command_id = api_ptr->command_journal().accept(CommandDomain::Scene, 0);
        api_ptr->command_journal().failed(command_id, ESP_ERR_INVALID_STATE);

        char detail[64];
        snprintf(detail, sizeof(detail), "Preset %u activation rejected", index);
        api::AuditLog::instance().log(api::ApiRole::Operator, "preset_activate_rejected", detail);

        return send_error(request, "409 Conflict", "scene_activation_rejected");
    }

    if (api_ptr->show_transport()) {
        api_ptr->show_transport()->set_scene(index);
        generation = api_ptr->show_transport()->generation();
    }

    const uint32_t command_id = api_ptr->command_journal().accept(CommandDomain::Scene, generation);

    api_ptr->signage()->feed_watchdog();
    api_ptr->signage()->release_control(ControlSource::EStop);

    char detail[64];
    snprintf(detail, sizeof(detail), "Preset %u activation accepted", index);
    api::AuditLog::instance().log(api::ApiRole::Operator, "preset_activate", detail);

    api_ptr->notify_status_async();

    char response[128];
    snprintf(
        response,
        sizeof(response),
        "{\"accepted\":true,\"commandId\":%" PRIu32 ",\"preset\":%u,\"generation\":%" PRIu32 "}",
        command_id,
        index,
        generation
    );

    return send_json(request, response, "202 Accepted");
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
    cJSON_AddNumberToObject(response, "syncToken", cfg.sync_token);
    cJSON_AddBoolToObject(response, "mqttEnabled", cfg.mqtt_enabled);
    cJSON_AddStringToObject(response, "mqttBroker", cfg.mqtt_broker);
    cJSON_AddNumberToObject(response, "mqttPort", cfg.mqtt_port);

    cJSON* channels = cJSON_CreateArray();
    for (uint8_t i = 0; i < 16; ++i) {
        const auto& c = cfg.channels[i];
        auto hw = channel_hardware(i);
        char port[8];
        channel_port_name(i, port, sizeof(port));

        const bool is_configured = channel_is_configured(hw, c);

        cJSON* ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "index", i);
        cJSON_AddStringToObject(ch, "port", port);
        cJSON_AddStringToObject(ch, "kind", hw.kind == ChannelKind::Addressable ? "addressable" : "pwm");
        cJSON_AddNumberToObject(ch, "hardwareId", hw.hardware_id);
        if (hw.kind == ChannelKind::Addressable) {
            cJSON_AddNumberToObject(ch, "gpio", hw.gpio);
        }
        cJSON_AddBoolToObject(ch, "configured", is_configured);
        cJSON_AddBoolToObject(ch, "enabled", c.enabled);
        cJSON_AddStringToObject(ch, "name", c.name);
        cJSON_AddNumberToObject(ch, "maxBrightness", c.max_brightness);
        cJSON_AddBoolToObject(ch, "gammaCorrection", c.gamma_correction);
        cJSON_AddNumberToObject(ch, "maxMilliwatts", c.max_milliwatts);
        cJSON_AddNumberToObject(ch, "dmxUniverse", c.dmx_universe);
        cJSON_AddNumberToObject(ch, "dmxStartChannel", c.dmx_start_channel);
        cJSON_AddNumberToObject(ch, "dmxMode", c.dmx_mode);

        if (hw.kind == ChannelKind::Addressable) {
            cJSON* addr = cJSON_CreateObject();
            cJSON_AddStringToObject(addr, "type", c.addressable_type == AddressableType::SK6812 ? "SK6812" : "WS2812");
            cJSON_AddNumberToObject(addr, "pixels", c.pixel_count);
            cJSON_AddStringToObject(addr, "pixelOrder", pixel_order_name(c.pixel_order));
            cJSON_AddItemToObject(ch, "addressable", addr);
        }

        cJSON_AddItemToArray(channels, ch);
    }
    cJSON_AddItemToObject(response, "channels", channels);

    cJSON* groups = cJSON_CreateArray();
    for (uint8_t i = 0; i < 8; ++i) {
        const auto& g = cfg.groups[i];
        cJSON* grp = cJSON_CreateObject();
        cJSON_AddNumberToObject(grp, "index", i);
        cJSON_AddStringToObject(grp, "name", g.name);
        cJSON_AddNumberToObject(grp, "channelMask", g.channel_mask);
        cJSON_AddStringToObject(grp, "role", zone_role_name(g.role));
        cJSON_AddBoolToObject(grp, "enabled", g.enabled);
        cJSON_AddItemToArray(groups, grp);
    }
    cJSON_AddItemToObject(response, "groups", groups);

    // Legacy synthesis for older dashboards
    cJSON* zones = cJSON_CreateArray();
    for (uint8_t i = 0; i < 8; ++i) {
        cJSON* z = cJSON_CreateObject();
        cJSON_AddStringToObject(z, "name", cfg.groups[i].name);
        cJSON_AddStringToObject(z, "role", zone_role_name(cfg.groups[i].role));
        cJSON_AddItemToArray(zones, z);
    }
    cJSON_AddItemToObject(response, "zones", zones);

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
    DeviceConfig old_cfg = cfg;

    VALIDATE_STRING("deviceName", sizeof(cfg.device_name), cfg.device_name);
    VALIDATE_STRING("serialNumber", sizeof(cfg.serial_number), cfg.serial_number);
    VALIDATE_STRING("hardwareRevision", sizeof(cfg.hardware_revision), cfg.hardware_revision);
    VALIDATE_STRING("siteLabel", sizeof(cfg.site_label), cfg.site_label);
    VALIDATE_STRING("groupLabel", sizeof(cfg.group_label), cfg.group_label);

    const cJSON* ap = cJSON_GetObjectItemCaseSensitive(root, "activePreset");
    if (!ap) ap = cJSON_GetObjectItemCaseSensitive(root, "activePresetIndex");
    if (ap) {
        if (!cJSON_IsNumber(ap) || ap->valuedouble < 0 || ap->valuedouble > (kMaxPresets - 1)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_activePreset");
        }
        cfg.active_preset_index = static_cast<uint8_t>(ap->valueint);
    }

    const cJSON* fp = cJSON_GetObjectItemCaseSensitive(root, "fallbackPreset");
    if (!fp) fp = cJSON_GetObjectItemCaseSensitive(root, "fallbackPresetIndex");
    if (fp) {
        if (!cJSON_IsNumber(fp) || fp->valuedouble < 0 || fp->valuedouble > (kMaxPresets - 1)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_fallbackPreset");
        }
        cfg.fallback_preset_index = static_cast<uint8_t>(fp->valueint);
    }

    VALIDATE_U16("watchdogTimeoutS", 0, 3600, cfg.watchdog_timeout_s);
    VALIDATE_U16("maxPowerMw", 0, 60000, cfg.max_power_mw);
    VALIDATE_U16("softStartMs", 0, 5000, cfg.soft_start_ms);

    const cJSON* gm = cJSON_GetObjectItemCaseSensitive(root, "gamma");
    if (!gm) gm = cJSON_GetObjectItemCaseSensitive(root, "gammaX10");
    if (gm) {
        if (!cJSON_IsNumber(gm)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_gamma");
        }
        if (gm->valuedouble >= 10.0 && gm->valuedouble <= 30.0) {
            cfg.gamma_x10 = static_cast<uint8_t>(gm->valueint);
        } else if (gm->valuedouble >= 1.0 && gm->valuedouble <= 3.0) {
            cfg.gamma_x10 = static_cast<uint8_t>(std::round(gm->valuedouble * 10.0));
        }
    }

    const cJSON* ss = cJSON_GetObjectItemCaseSensitive(root, "softStart");
    if (!ss) ss = cJSON_GetObjectItemCaseSensitive(root, "softStartEnabled");
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

    const cJSON* st = cJSON_GetObjectItemCaseSensitive(root, "syncToken");
    if (st) {
        if (!cJSON_IsNumber(st) || st->valuedouble < 0 || st->valuedouble > 4294967295.0) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_syncToken");
        }
        cfg.sync_token = static_cast<uint32_t>(st->valuedouble);
    }

    const cJSON* channels_arr = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (channels_arr && cJSON_IsArray(channels_arr)) {
        int ch_idx = 0;
        cJSON* ch_item = nullptr;
        cJSON_ArrayForEach(ch_item, channels_arr) {
            if (ch_idx >= 16) break;
            const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(ch_item, "name");
            if (name_j && cJSON_IsString(name_j)) {
                strncpy(cfg.channels[ch_idx].name, name_j->valuestring, sizeof(cfg.channels[ch_idx].name) - 1);
                cfg.channels[ch_idx].name[sizeof(cfg.channels[ch_idx].name) - 1] = '\0';
            }
            const cJSON* en_j = cJSON_GetObjectItemCaseSensitive(ch_item, "enabled");
            if (en_j && cJSON_IsBool(en_j)) cfg.channels[ch_idx].enabled = cJSON_IsTrue(en_j);

            const cJSON* mb_j = cJSON_GetObjectItemCaseSensitive(ch_item, "maxBrightness");
            if (mb_j && cJSON_IsNumber(mb_j)) cfg.channels[ch_idx].max_brightness = static_cast<uint8_t>(mb_j->valueint);

            const cJSON* gc_j = cJSON_GetObjectItemCaseSensitive(ch_item, "gammaCorrection");
            if (gc_j && cJSON_IsBool(gc_j)) cfg.channels[ch_idx].gamma_correction = cJSON_IsTrue(gc_j);

            const cJSON* mw_j = cJSON_GetObjectItemCaseSensitive(ch_item, "maxMilliwatts");
            if (mw_j && cJSON_IsNumber(mw_j)) cfg.channels[ch_idx].max_milliwatts = static_cast<uint16_t>(mw_j->valueint);

            const cJSON* du_j = cJSON_GetObjectItemCaseSensitive(ch_item, "dmxUniverse");
            if (du_j && cJSON_IsNumber(du_j)) cfg.channels[ch_idx].dmx_universe = static_cast<uint16_t>(du_j->valueint);

            const cJSON* ds_j = cJSON_GetObjectItemCaseSensitive(ch_item, "dmxStartChannel");
            if (ds_j && cJSON_IsNumber(ds_j)) cfg.channels[ch_idx].dmx_start_channel = static_cast<uint16_t>(ds_j->valueint);

            const cJSON* dm_j = cJSON_GetObjectItemCaseSensitive(ch_item, "dmxMode");
            if (dm_j && cJSON_IsNumber(dm_j)) cfg.channels[ch_idx].dmx_mode = static_cast<uint8_t>(dm_j->valueint);

            const cJSON* addr_obj = cJSON_GetObjectItemCaseSensitive(ch_item, "addressable");
            const cJSON* src_addr = addr_obj ? addr_obj : ch_item;
            const cJSON* atype_j = cJSON_GetObjectItemCaseSensitive(src_addr, "type");
            if (!atype_j) atype_j = cJSON_GetObjectItemCaseSensitive(src_addr, "addressableType");
            if (atype_j) {
                if (cJSON_IsString(atype_j)) {
                    cfg.channels[ch_idx].addressable_type = (strcmp(atype_j->valuestring, "SK6812") == 0) ? AddressableType::SK6812 : AddressableType::WS2812;
                } else if (cJSON_IsNumber(atype_j)) {
                    cfg.channels[ch_idx].addressable_type = static_cast<AddressableType>(atype_j->valueint);
                }
            }
            const cJSON* px_j = cJSON_GetObjectItemCaseSensitive(src_addr, "pixels");
            if (!px_j) px_j = cJSON_GetObjectItemCaseSensitive(src_addr, "pixelCount");
            if (px_j && cJSON_IsNumber(px_j)) {
                cfg.channels[ch_idx].pixel_count = static_cast<uint16_t>(std::min(px_j->valueint, static_cast<int>(MAX_PIXELS)));
            }
            const cJSON* po_j = cJSON_GetObjectItemCaseSensitive(src_addr, "pixelOrder");
            if (po_j) {
                if (cJSON_IsNumber(po_j)) cfg.channels[ch_idx].pixel_order = static_cast<PixelOrder>(po_j->valueint);
                else if (cJSON_IsString(po_j)) parse_pixel_order(po_j->valuestring, cfg.channels[ch_idx].pixel_order);
            }

            ++ch_idx;
        }
    }

    const cJSON* groups_arr = cJSON_GetObjectItemCaseSensitive(root, "groups");
    if (groups_arr && cJSON_IsArray(groups_arr)) {
        int g_idx = 0;
        cJSON* g_item = nullptr;
        cJSON_ArrayForEach(g_item, groups_arr) {
            if (g_idx >= 8) break;
            const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(g_item, "name");
            if (name_j && cJSON_IsString(name_j)) {
                strncpy(cfg.groups[g_idx].name, name_j->valuestring, sizeof(cfg.groups[g_idx].name) - 1);
                cfg.groups[g_idx].name[sizeof(cfg.groups[g_idx].name) - 1] = '\0';
            }
            const cJSON* mask_j = cJSON_GetObjectItemCaseSensitive(g_item, "channelMask");
            if (mask_j && cJSON_IsNumber(mask_j)) cfg.groups[g_idx].channel_mask = static_cast<uint16_t>(mask_j->valueint);

            const cJSON* en_j = cJSON_GetObjectItemCaseSensitive(g_item, "enabled");
            if (en_j && cJSON_IsBool(en_j)) cfg.groups[g_idx].enabled = cJSON_IsTrue(en_j);

            const cJSON* role_j = cJSON_GetObjectItemCaseSensitive(g_item, "role");
            if (role_j && cJSON_IsString(role_j)) parse_zone_role(role_j->valuestring, cfg.groups[g_idx].role);

            ++g_idx;
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

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Config);
    if (api_ptr->config()->save_config(cfg) != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_NVS_NOT_FOUND);
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    bool sync_changed = (old_cfg.sync_enabled != cfg.sync_enabled) ||
                        (old_cfg.sync_role != cfg.sync_role) ||
                        (old_cfg.sync_token != cfg.sync_token);
    if (sync_changed && api_ptr->config_apply_callback()) {
        const auto apply_err = api_ptr->config_apply_callback()(old_cfg, cfg);
        if (apply_err != ESP_OK) {
            api_ptr->config()->save_config(old_cfg);
            const auto rollback_err = api_ptr->config_apply_callback()(cfg, old_cfg);
            if (rollback_err != ESP_OK) {
                api::AuditLog::instance().log(api::ApiRole::Admin, "sync_reconfig_rollback_failed", "Sync reconfig rollback failed");
                api_ptr->command_journal().failed(cmd_id, rollback_err);
                return send_error(request, "500 Internal Server Error", "sync_reconfig_rollback_failed");
            }
            api::AuditLog::instance().log(api::ApiRole::Admin, "sync_reconfig_failed", "Sync reconfig failed, rolled back");
            api_ptr->command_journal().failed(cmd_id, apply_err);
            return send_error(request, "500 Internal Server Error", "sync_reconfig_failed");
        }
    }

    if (api_ptr->signage()) {
        api_ptr->signage()->reload_device_config();
    }
    
    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    api::AuditLog::instance().log(api::ApiRole::Admin, "device_config", "Config updated");
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"saved\":true,\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp);
}

esp_err_t get_network_config(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    NetworkSettings net_settings;
    api_ptr->config()->load_network_settings(net_settings);

    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "mode", network_mode_to_string(net_settings.mode));
    cJSON_AddStringToObject(response, "ssid", net_settings.sta_ssid);
    cJSON_AddBoolToObject(response, "passwordSet", (net_settings.sta_password[0] != '\0'));
    cJSON_AddStringToObject(response, "apName", net_settings.ap_name);
    cJSON_AddNumberToObject(response, "apChannel", net_settings.ap_channel);

    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }

    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t put_network_config(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    // Safe mode invariant: recovery networking cannot be disabled or altered away from recovery
    if (api_ptr->safe_mode_check_ && api_ptr->safe_mode_check_()) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "safe_mode_network_locked");
    }

    NetworkSettings previous{};
    api_ptr->config()->load_network_settings(previous);
    NetworkSettings desired = previous;

    // mode validation
    const cJSON* mode_item = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (mode_item) {
        if (!cJSON_IsString(mode_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_network_mode");
        }
        NetworkMode mode;
        if (!parse_network_mode(mode_item->valuestring, mode)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_network_mode");
        }
        desired.mode = mode;
    }

    // ssid validation
    const cJSON* ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    if (ssid_item) {
        if (!cJSON_IsString(ssid_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ssid");
        }
        size_t len = std::strlen(ssid_item->valuestring);
        if (len > 32) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ssid");
        }
        std::strncpy(desired.sta_ssid, ssid_item->valuestring, sizeof(desired.sta_ssid) - 1);
        desired.sta_ssid[sizeof(desired.sta_ssid) - 1] = '\0';
    }

    // SSID is strictly required for sta_only and sta_with_ap_fallback
    if (desired.mode == NetworkMode::StaOnly || desired.mode == NetworkMode::StaWithApFallback) {
        if (desired.sta_ssid[0] == '\0') {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ssid");
        }
    }

    // clearPassword check
    const cJSON* clear_pwd = cJSON_GetObjectItemCaseSensitive(root, "clearPassword");
    if (clear_pwd) {
        if (!cJSON_IsBool(clear_pwd)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_json");
        }
        if (cJSON_IsTrue(clear_pwd)) {
            desired.sta_password[0] = '\0';
        }
    }

    // password check
    const cJSON* pwd_item = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (pwd_item) {
        if (!cJSON_IsString(pwd_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_password");
        }
        const char* pwd_str = pwd_item->valuestring;
        // Non-empty password: validate WPA/WPA2 passphrase bounds (8..63 chars)
        if (pwd_str != nullptr && pwd_str[0] != '\0') {
            size_t len = std::strlen(pwd_str);
            if (len < 8 || len > 63) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_password");
            }
            std::strncpy(desired.sta_password, pwd_str, sizeof(desired.sta_password) - 1);
            desired.sta_password[sizeof(desired.sta_password) - 1] = '\0';
        }
    }

    // apName validation
    const cJSON* ap_name_item = cJSON_GetObjectItemCaseSensitive(root, "apName");
    if (ap_name_item) {
        if (!cJSON_IsString(ap_name_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ap_name");
        }
        size_t len = std::strlen(ap_name_item->valuestring);
        if (len < 1 || len > 32) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ap_name");
        }
        std::strncpy(desired.ap_name, ap_name_item->valuestring, sizeof(desired.ap_name) - 1);
        desired.ap_name[sizeof(desired.ap_name) - 1] = '\0';
    }

    // apChannel validation (1..13 only; 14 must be rejected)
    const cJSON* ap_ch_item = cJSON_GetObjectItemCaseSensitive(root, "apChannel");
    if (ap_ch_item) {
        if (!cJSON_IsNumber(ap_ch_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ap_channel");
        }
        int ch = ap_ch_item->valueint;
        if (ch < 1 || ch > 13 || ap_ch_item->valuedouble != static_cast<double>(ch)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ap_channel");
        }
        desired.ap_channel = static_cast<uint8_t>(ch);
    }

    // Ensure queue handler is available
    if (!api_ptr->network_apply_queue_) {
        cJSON_Delete(root);
        return send_error(request, "503 Service Unavailable", "network_apply_unavailable");
    }

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Config);
    NetworkApplyRequest apply_req{
        .previous = previous,
        .desired = desired,
        .command_id = cmd_id
    };

    esp_err_t queue_err = api_ptr->network_apply_queue_(apply_req);
    if (queue_err == ESP_ERR_INVALID_STATE) {
        api_ptr->command_journal().failed(cmd_id, queue_err);
        cJSON_Delete(root);
        return send_error(request, "409 Conflict", "network_apply_in_progress");
    } else if (queue_err != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, queue_err);
        cJSON_Delete(root);
        return send_error(request, "500 Internal Server Error", "network_queue_failed");
    }

    api::AuditLog::instance().log(api::ApiRole::Admin, "network_config", "Network apply queued");
    cJSON_Delete(root);

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 ",\"applyPending\":true}", cmd_id);
    return send_json(request, resp, "202 Accepted");
}

esp_err_t get_network_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);

    NetworkApplyState apply_st = NetworkApplyState::Idle;
    uint32_t pending_cmd = 0;
    const char* last_err = nullptr;
    if (api_ptr->network_apply_status_) {
        api_ptr->network_apply_status_(apply_st, pending_cmd, last_err);
    }

    cJSON* response = cJSON_CreateObject();
    if (api_ptr->wifi()) {
        WifiRuntimeStatus st = api_ptr->wifi()->status();

        const char* state_str = "idle";
        if (st.is_fallback_ap) {
            state_str = "fallback_ap";
        } else if (st.sta_connected) {
            state_str = "connected";
        } else if (st.state == WifiState::Connecting) {
            state_str = "connecting";
        } else if (st.state == WifiState::ApActive) {
            state_str = "ap_active";
        } else if (st.state == WifiState::Failed) {
            state_str = "failed";
        } else {
            state_str = "idle";
        }

        cJSON_AddStringToObject(response, "state", state_str);
        cJSON_AddStringToObject(response, "mode",
            st.mode == WifiMode::StaOnly ? "sta_only" :
            st.mode == WifiMode::ApOnly ? "ap_only" : "sta_with_ap_fallback");
        cJSON_AddBoolToObject(response, "staConnected", st.sta_connected);
        cJSON_AddStringToObject(response, "staSsid", st.sta_ssid);
        cJSON_AddNumberToObject(response, "staRssi", st.sta_rssi);

        char sta_ip_str[16] = {};
        snprintf(sta_ip_str, sizeof(sta_ip_str), IPSTR, IP2STR(&st.sta_ip));
        cJSON_AddStringToObject(response, "staIp", sta_ip_str);

        cJSON_AddBoolToObject(response, "apActive", st.ap_active);

        char ap_ip_str[16] = {};
        snprintf(ap_ip_str, sizeof(ap_ip_str), IPSTR, IP2STR(&st.ap_ip));
        cJSON_AddStringToObject(response, "apIp", ap_ip_str);

        cJSON_AddNumberToObject(response, "retryCount", st.retry_count);
        cJSON_AddStringToObject(response, "applyState", network_apply_state_to_string(apply_st));

        if (pending_cmd != 0) {
            cJSON_AddNumberToObject(response, "pendingCommandId", pending_cmd);
        } else {
            cJSON_AddNullToObject(response, "pendingCommandId");
        }

        if (last_err != nullptr) {
            cJSON_AddStringToObject(response, "lastApplyError", last_err);
        } else {
            cJSON_AddNullToObject(response, "lastApplyError");
        }

        cJSON_AddNumberToObject(response, "lastDisconnectReason", st.last_disconnect_reason);

        NetworkSettings persisted{};
        bool persisted_matches = false;
        if (api_ptr->config() && api_ptr->config()->load_network_settings(persisted) == ESP_OK) {
            NetworkMode current_mode = (st.mode == WifiMode::StaOnly) ? NetworkMode::StaOnly :
                (st.mode == WifiMode::ApOnly) ? NetworkMode::ApOnly : NetworkMode::StaWithApFallback;
            if (persisted.mode == current_mode &&
                std::strcmp(persisted.sta_ssid, st.sta_ssid) == 0 &&
                !st.is_fallback_ap &&
                apply_st == NetworkApplyState::Idle &&
                last_err == nullptr) {
                persisted_matches = true;
            }
        }
        cJSON_AddBoolToObject(response, "persistedMatchesRuntime", persisted_matches);
    } else {
        cJSON_AddStringToObject(response, "state", "unavailable");
        cJSON_AddStringToObject(response, "applyState", network_apply_state_to_string(apply_st));
        cJSON_AddNullToObject(response, "pendingCommandId");
        cJSON_AddNullToObject(response, "lastApplyError");
        cJSON_AddBoolToObject(response, "persistedMatchesRuntime", false);
    }

    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == nullptr) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }

    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t get_channels(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    DeviceConfig cfg;
    api_ptr->config()->load_config(cfg);

    cJSON* response = cJSON_CreateObject();
    cJSON* ch_arr = cJSON_CreateArray();

    for (uint8_t i = 0; i < kChannelCount; ++i) {
        const auto& c = cfg.channels[i];
        auto hw = channel_hardware(i);
        char port[8];
        channel_port_name(i, port, sizeof(port));

        // Hardware identity is not configuration. A1/P1 always exist even when
        // nothing is connected; only electrical/runtime settings make a port configured.
        const bool configured = channel_is_configured(i, c);

        cJSON* ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "index", i);
        cJSON_AddStringToObject(ch, "port", port);
        cJSON_AddStringToObject(ch, "kind", hw.kind == ChannelKind::Addressable ? "addressable" : "pwm");
        cJSON_AddNumberToObject(ch, "hardwareId", hw.hardware_id);
        if (hw.kind == ChannelKind::Addressable) {
            cJSON_AddNumberToObject(ch, "gpio", hw.gpio);
        }
        cJSON_AddBoolToObject(ch, "configured", configured);
        cJSON_AddBoolToObject(ch, "enabled", c.enabled);
        cJSON_AddStringToObject(ch, "name", c.name);
        cJSON_AddNumberToObject(ch, "maxBrightness", c.max_brightness);
        cJSON_AddBoolToObject(ch, "gammaCorrection", c.gamma_correction);
        cJSON_AddNumberToObject(ch, "maxMilliwatts", c.max_milliwatts);
        cJSON_AddNumberToObject(ch, "dmxUniverse", c.dmx_universe);
        cJSON_AddNumberToObject(ch, "dmxStartChannel", c.dmx_start_channel);
        cJSON_AddNumberToObject(ch, "dmxMode", c.dmx_mode);

        if (hw.kind == ChannelKind::Addressable) {
            cJSON* addr = cJSON_CreateObject();
            cJSON_AddStringToObject(addr, "type", c.addressable_type == AddressableType::SK6812 ? "SK6812" : "WS2812");
            cJSON_AddNumberToObject(addr, "pixels", c.pixel_count);
            cJSON_AddStringToObject(addr, "pixelOrder", pixel_order_name(c.pixel_order));
            cJSON_AddItemToObject(ch, "addressable", addr);
        }

        cJSON_AddItemToArray(ch_arr, ch);
    }
    cJSON_AddItemToObject(response, "channels", ch_arr);

    char* json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!json) return send_error(request, "500 Internal Server Error", "json_encode_failed");

    const esp_err_t result = send_json(request, json);
    cJSON_free(json);
    return result;
}

esp_err_t post_channel_test(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t channel = 0;
    if (!json_u8(root, "channel", 0, 15, channel)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_field_channel_0_15");
    }

    char mode[32] = "identify";
    const cJSON* m_item = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (m_item && cJSON_IsString(m_item) && strlen(m_item->valuestring) < sizeof(mode)) {
        strncpy(mode, m_item->valuestring, sizeof(mode) - 1);
        mode[sizeof(mode) - 1] = '\0';
    }

    uint32_t duration_ms = 3000;
    const cJSON* d_item = cJSON_GetObjectItemCaseSensitive(root, "durationMs");
    if (d_item && cJSON_IsNumber(d_item) && d_item->valueint > 0 && d_item->valueint <= 60000) {
        duration_ms = static_cast<uint32_t>(d_item->valueint);
    }

    cJSON_Delete(root);

    bool accepted = api_ptr->runner().test_channel(channel, mode, duration_ms);
    if (!accepted) {
        return send_error(request, "503 Service Unavailable", "test_runner_busy");
    }

    char port[8];
    channel_port_name(channel, port, sizeof(port));

    char test_resp[128];
    snprintf(test_resp, sizeof(test_resp),
        "{\"accepted\":true,\"channel\":%u,\"port\":\"%s\",\"mode\":\"%s\",\"durationMs\":%" PRIu32 "}",
        channel, port, mode, duration_ms);

    return send_json(request, test_resp, "202 Accepted");
}

[[maybe_unused]] static const char* control_source_name(ControlSource src) {
    switch (src) {
        case ControlSource::EStop: return "EStop";
        case ControlSource::Factory: return "Factory";
        case ControlSource::LiveDmx: return "LiveDmx";
        case ControlSource::ManualOverride: return "ManualOverride";
        case ControlSource::Scheduler: return "Scheduler";
        case ControlSource::ScenePlayback: return "ScenePlayback";
        case ControlSource::Fallback: return "Fallback";
        case ControlSource::Idle: return "Idle";
        default: return "Unknown";
    }
}

esp_err_t get_signage_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);

    // Schema 3 Canonical RuntimeSnapshot: HTTP uses streamSeq = 0
    RuntimeSnapshot snapshot = collect_runtime_snapshot(api_ptr, 0);

    char payload[kRuntimeSnapshotMaxBytes];
    FixedJsonWriter writer(payload, sizeof(payload));
    if (!serialize_runtime_snapshot(snapshot, writer)) {
        return send_error(request, "500 Internal Server Error", "snapshot_serialization_failed");
    }

    return send_json(request, payload);
}

esp_err_t get_dmx_frame(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    char dmx_hex[1025];
    if (api_ptr->signage()) {
        const auto& dmx = api_ptr->signage()->last_dmx_data();
        for (int i = 0; i < 512; ++i) {
            snprintf(dmx_hex + i * 2, 3, "%02x", dmx[i]);
        }
    } else {
        memset(dmx_hex, '0', 1024);
    }
    dmx_hex[1024] = '\0';

    char body[1152];
    snprintf(body, sizeof(body), "{\"universe\":1,\"dataHex\":\"%s\"}", dmx_hex);
    return send_json(request, body);
}

esp_err_t get_identity(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    DeviceConfig cfg;
    const char* dev_name = nullptr;
    if (api_ptr->config() && api_ptr->config()->load_config(cfg) == ESP_OK) {
        dev_name = cfg.device_name;
    }
    NodeIdentity id = get_node_identity(dev_name);
    char body[256];
    snprintf(body, sizeof(body),
        "{\"id\":\"%s\",\"name\":\"%s\",\"family\":\"%s\",\"model\":\"%s\",\"hardwareRevision\":\"%s\",\"firmwareVersion\":\"%s\"}",
        id.id, id.name, id.family, id.model, id.hw_revision, id.fw_version);
    return send_json(request, body);
}

esp_err_t get_fleet_nodes(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    cJSON* root = cJSON_CreateObject();

    // Self info
    NodeIdentity self_id = api_ptr->identity();
    cJSON* self_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(self_obj, "id", self_id.id);
    cJSON_AddStringToObject(self_obj, "name", self_id.name);
    cJSON_AddStringToObject(self_obj, "hostname", self_id.id);
    cJSON_AddStringToObject(self_obj, "family", self_id.family);
    cJSON_AddStringToObject(self_obj, "model", self_id.model);
    cJSON_AddStringToObject(self_obj, "firmware", self_id.fw_version);
    cJSON_AddStringToObject(self_obj, "hardwareRevision", self_id.hw_revision);
    cJSON_AddItemToObject(root, "self", self_obj);

    // Discovered nodes
    cJSON* nodes_arr = cJSON_CreateArray();
    if (api_ptr->fleet_discovery()) {
        discovery::DiscoveredNode snap[discovery::FleetDiscoveryService::kMaxDiscoveredNodes];
        size_t n = api_ptr->fleet_discovery()->get_snapshot(snap, discovery::FleetDiscoveryService::kMaxDiscoveredNodes, now_ms);

        for (size_t i = 0; i < n; ++i) {
            cJSON* node_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(node_obj, "id", snap[i].id);
            cJSON_AddStringToObject(node_obj, "name", snap[i].name);
            cJSON_AddStringToObject(node_obj, "hostname", snap[i].hostname);

            char ip_str[16];
            discovery::FleetDiscoveryService::format_ipv4(snap[i].ipv4, ip_str);
            cJSON_AddStringToObject(node_obj, "ip", ip_str);
            cJSON_AddNumberToObject(node_obj, "port", snap[i].port);
            cJSON_AddStringToObject(node_obj, "family", snap[i].family);
            cJSON_AddStringToObject(node_obj, "model", snap[i].model);
            cJSON_AddStringToObject(node_obj, "firmware", snap[i].firmware);

            uint32_t age_ms = (now_ms >= snap[i].last_seen_ms) ? (now_ms - snap[i].last_seen_ms) : 0;
            cJSON_AddNumberToObject(node_obj, "lastSeenMs", snap[i].last_seen_ms);
            cJSON_AddNumberToObject(node_obj, "ageMs", age_ms);

            discovery::NodePresence presence = discovery::FleetDiscoveryService::determine_presence(snap[i].last_seen_ms, now_ms);
            cJSON_AddStringToObject(node_obj, "status", discovery::FleetDiscoveryService::presence_to_string(presence));

            cJSON_AddItemToArray(nodes_arr, node_obj);
        }
    }
    cJSON_AddItemToObject(root, "nodes", nodes_arr);
    return send_json(request, root, "200 OK");
}

esp_err_t get_capabilities(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    DeviceConfig cfg;
    const char* dev_name = nullptr;
    if (api_ptr->config() && api_ptr->config()->load_config(cfg) == ESP_OK) {
        dev_name = cfg.device_name;
    }
    NodeIdentity id = get_node_identity(dev_name);
    char body[1536];
    snprintf(body, sizeof(body),
        "{\"schema\":%u,"
        "\"node\":{\"family\":\"%s\",\"model\":\"%s\",\"firmware\":\"%s\",\"hardwareRevision\":\"%s\"},"
        "\"limits\":{\"channels\":16,\"groups\":8,\"pixelLanes\":%u,\"pwmChannels\":%u,\"zones\":%u,\"layersPerZone\":%u,\"presets\":%u,\"timelineSteps\":%u,\"cueLists\":%u,\"cuesPerList\":%u,\"automationTracks\":%u,\"automationPoints\":%u,\"syncPeers\":%u,\"syncProtocol\":%u},"
        "\"features\":{\"manualControl\":true,\"addressable\":%s,\"pwm\":%s,\"artnet\":%s,\"sacn\":%s,\"sync\":%s,\"mqtt\":%s,\"factory\":%s,\"ota\":%s,\"signedOta\":%s,\"backup\":%s,\"sceneMotion\":%s,\"cueLists\":%s,\"transitionEngineV2\":%s,\"showTransportV2\":%s,\"syncCueTransport\":%s,\"syncTimelineTransport\":%s,\"showManifest\":%s,\"playbackManifest\":%s,\"fleetDiscovery\":%s},"
        "\"protocols\":{\"httpApi\":%u,\"websocket\":%u,\"sync\":%u,\"discovery\":%u}}",
        CapabilitiesManifest::kSchema,
        id.family, id.model, id.fw_version, id.hw_revision,
        CapabilitiesManifest::kPixelLanes, CapabilitiesManifest::kPwmChannels,
        CapabilitiesManifest::kZones, CapabilitiesManifest::kLayersPerZone,
        CapabilitiesManifest::kPresets, CapabilitiesManifest::kTimelineSteps,
        CapabilitiesManifest::kCueLists, CapabilitiesManifest::kCuesPerList,
        CapabilitiesManifest::kAutomationTracks, CapabilitiesManifest::kAutomationPoints,
        CapabilitiesManifest::kSyncPeers,
        CapabilitiesManifest::kProtocolSync,
        CapabilitiesManifest::kFeatureAddressable ? "true" : "false",
        CapabilitiesManifest::kFeaturePwm ? "true" : "false",
        CapabilitiesManifest::kFeatureArtnet ? "true" : "false",
        CapabilitiesManifest::kFeatureSacn ? "true" : "false",
        CapabilitiesManifest::kFeatureSync ? "true" : "false",
        CapabilitiesManifest::kFeatureMqtt ? "true" : "false",
        CapabilitiesManifest::kFeatureFactory ? "true" : "false",
        CapabilitiesManifest::kFeatureOta ? "true" : "false",
        CapabilitiesManifest::kFeatureSignedOta ? "true" : "false",
        CapabilitiesManifest::kFeatureBackup ? "true" : "false",
        CapabilitiesManifest::kFeatureSceneMotion ? "true" : "false",
        CapabilitiesManifest::kFeatureCueLists ? "true" : "false",
        CapabilitiesManifest::kFeatureTransitionEngineV2 ? "true" : "false",
        CapabilitiesManifest::kFeatureShowTransportV2 ? "true" : "false",
        CapabilitiesManifest::kFeatureSyncCueTransport ? "true" : "false",
        CapabilitiesManifest::kFeatureSyncTimelineTransport ? "true" : "false",
        CapabilitiesManifest::kFeatureShowManifest ? "true" : "false",
        CapabilitiesManifest::kFeaturePlaybackManifest ? "true" : "false",
        CapabilitiesManifest::kFeatureFleetDiscovery ? "true" : "false",
        CapabilitiesManifest::kProtocolHttpApi,
        CapabilitiesManifest::kProtocolWebsocket,
        CapabilitiesManifest::kProtocolSync,
        CapabilitiesManifest::kProtocolDiscovery
    );
    return send_json(request, body);
}

esp_err_t get_current_job(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;
    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    OperationJob job;
    if (api_ptr->job_manager().get_current(job)) {
        char body[256];
        snprintf(body, sizeof(body),
            "{\"jobId\":%" PRIu32 ",\"type\":\"%s\",\"state\":\"%s\",\"progress\":%u,\"startedMs\":%" PRIu32 ",\"error\":%s}",
            job.job_id, job_type_name(job.type), job_state_name(job.state), job.progress, job.started_ms,
            job.error_code != 0 ? "\"failed\"" : "null");
        return send_json(request, body);
    }
    return send_json(request, "{\"job\":null}");
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
    for (uint8_t i = 0; i < 16; ++i) {
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
            if (trk_idx >= 16) break;
            json_u8(trk_item, "channelId", 0, 15, seq.tracks[trk_idx].channel_id);
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

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Timeline);
    if (api_ptr->config()->save_sequence(seq) != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_NVS_NOT_FOUND);
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    api::AuditLog::instance().log(api::ApiRole::Operator, "sequence_update", "Sequence updated");

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"saved\":true,\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp);
}

esp_err_t put_manual(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->signage()) return send_error(request, "503 Service Unavailable", "signage_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint32_t lease_ms = 15000;
    const cJSON* lease_j = cJSON_GetObjectItemCaseSensitive(root, "leaseMs");
    if (lease_j && cJSON_IsNumber(lease_j)) {
        int l = lease_j->valueint;
        if (l < 1000) l = 1000;
        if (l > 60000) l = 60000;
        lease_ms = static_cast<uint32_t>(l);
    }

    const cJSON* channels_arr = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (!channels_arr || !cJSON_IsArray(channels_arr)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_channels_array");
    }

    int num_entries = cJSON_GetArraySize(channels_arr);
    if (num_entries <= 0) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "empty_channels_use_delete");
    }
    if (num_entries > 16) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "too_many_channels");
    }

    DeviceConfig cfg = api_ptr->signage()->get_config_snapshot();
    ManualOverrideFrame frame{};
    bool index_seen[16] = {};

    cJSON* ch_item = nullptr;
    cJSON_ArrayForEach(ch_item, channels_arr) {
        if (!cJSON_IsObject(ch_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_channel_item");
        }

        const cJSON* idx_j = cJSON_GetObjectItemCaseSensitive(ch_item, "index");
        if (!idx_j || !cJSON_IsNumber(idx_j) || idx_j->valueint < 0 || idx_j->valueint > 15) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_channel_index");
        }

        uint8_t ch_idx = static_cast<uint8_t>(idx_j->valueint);
        if (index_seen[ch_idx]) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "duplicate_channel_index");
        }
        index_seen[ch_idx] = true;

        const auto& ch_cfg = cfg.channels[ch_idx];
        auto hw = channel_hardware(ch_idx);

        const bool is_configured = channel_is_configured(hw, ch_cfg);

        if (!is_configured || !ch_cfg.enabled) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "channel_disabled_or_unconfigured");
        }

        if (hw.kind == ChannelKind::Addressable) {
            if (cJSON_GetObjectItemCaseSensitive(ch_item, "pwm")) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "pwm_not_allowed_on_addressable");
            }

            const cJSON* r_j = cJSON_GetObjectItemCaseSensitive(ch_item, "r");
            const cJSON* g_j = cJSON_GetObjectItemCaseSensitive(ch_item, "g");
            const cJSON* b_j = cJSON_GetObjectItemCaseSensitive(ch_item, "b");
            const cJSON* br_j = cJSON_GetObjectItemCaseSensitive(ch_item, "brightness");

            int r = (r_j && cJSON_IsNumber(r_j)) ? r_j->valueint : 0;
            int g = (g_j && cJSON_IsNumber(g_j)) ? g_j->valueint : 0;
            int b = (b_j && cJSON_IsNumber(b_j)) ? b_j->valueint : 0;
            int br = (br_j && cJSON_IsNumber(br_j)) ? br_j->valueint : 255;

            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || br < 0 || br > 255) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_color_or_brightness_value");
            }

            frame.channels[ch_idx].active = true;
            frame.channels[ch_idx].color.r = static_cast<uint8_t>(r);
            frame.channels[ch_idx].color.g = static_cast<uint8_t>(g);
            frame.channels[ch_idx].color.b = static_cast<uint8_t>(b);
            frame.channels[ch_idx].color.w = 0;
            frame.channels[ch_idx].brightness = static_cast<uint8_t>(br);
        } else {
            if (cJSON_GetObjectItemCaseSensitive(ch_item, "r") ||
                cJSON_GetObjectItemCaseSensitive(ch_item, "g") ||
                cJSON_GetObjectItemCaseSensitive(ch_item, "b") ||
                cJSON_GetObjectItemCaseSensitive(ch_item, "brightness")) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "color_not_allowed_on_pwm");
            }

            const cJSON* pwm_j = cJSON_GetObjectItemCaseSensitive(ch_item, "pwm");
            if (!pwm_j || !cJSON_IsNumber(pwm_j) || pwm_j->valueint < 0 || pwm_j->valueint > 4095) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_pwm_duty");
            }

            frame.channels[ch_idx].active = true;
            frame.channels[ch_idx].pwm = static_cast<uint16_t>(pwm_j->valueint);
        }
    }

    cJSON_Delete(root);

    uint16_t channel_mask = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        if (frame.channels[i].active) channel_mask |= (1U << i);
    }

    if (!api_ptr->signage()->submit_manual_override(frame, lease_ms)) {
        return send_error(request, "500 Internal Server Error", "submit_manual_override_failed");
    }

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"channelMask\":%u,\"leaseMs\":%" PRIu32 "}", channel_mask, lease_ms);
    return send_json(request, resp);
}

esp_err_t delete_manual(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->signage()) return send_error(request, "503 Service Unavailable", "signage_not_ready");

    api_ptr->signage()->clear_manual_override();
    return send_json(request, "{\"released\":true}");
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

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Schedule);
    if (api_ptr->config()->save_calendar_schedule(schedule) != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_NVS_NOT_FOUND);
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();
    
    api::AuditLog::instance().log(api::ApiRole::Operator, "schedule_update", "Calendar schedule updated");

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"saved\":true,\"accepted\":true,\"commandId\":%" PRIu32 "}", cmd_id);
    return send_json(request, resp);
}

esp_err_t get_sync_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->node_sync()) {
        return send_error(request, "503 Service Unavailable", "sync_not_ready");
    }

    const NodeSyncStatus status = api_ptr->node_sync()->status();
    cJSON* response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocolVersion", 4);
    cJSON_AddNumberToObject(response, "syncProtocol", 4);

    DeviceConfig dev_cfg{};
    if (api_ptr->config() && api_ptr->config()->load_config(dev_cfg) == ESP_OK) {
        cJSON_AddBoolToObject(response, "syncEnabled", dev_cfg.sync_enabled);
        cJSON_AddStringToObject(response, "groupLabel", dev_cfg.group_label);
    } else {
        cJSON_AddBoolToObject(response, "syncEnabled", false);
        cJSON_AddStringToObject(response, "groupLabel", "");
    }

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

    if (status.master_address == 0) {
        cJSON_AddNullToObject(response, "masterAddress");
    } else {
        char master_str[16] = {};
        format_ipv4(status.master_address, master_str, sizeof(master_str));
        cJSON_AddStringToObject(response, "masterAddress", master_str);
    }
    const char* transport_str = "standalone";
    if (status.transport == 1) {
        transport_str = "scene";
    } else if (status.transport == 2) {
        transport_str = "timeline";
    } else if (status.transport == 3) {
        transport_str = "cue";
    }
    cJSON_AddStringToObject(response, "transport", transport_str);
    cJSON_AddNumberToObject(response, "transportGeneration", status.transport_generation);
    cJSON_AddBoolToObject(response, "transportApplyOk", status.transport_apply_ok);

    const char* err_str = "none";
    switch (status.transport_error) {
        case 1: err_str = "scene_missing"; break;
        case 2: err_str = "cue_list_missing"; break;
        case 3: err_str = "cue_index_invalid"; break;
        case 4: err_str = "runtime_unavailable"; break;
        case 5: err_str = "unsupported_transport"; break;
        default: err_str = "none"; break;
    }
    cJSON_AddStringToObject(response, "transportError", err_str);

    if (status.remote_bpm == 0) {
        cJSON_AddNullToObject(response, "remoteBpm");
    } else {
        cJSON_AddNumberToObject(response, "remoteBpm", status.remote_bpm);
    }
    cJSON_AddBoolToObject(response, "remoteTimelineEnabled", status.remote_timeline_enabled);

    if (status.transport == 3) {
        cJSON* cue_obj = cJSON_CreateObject();
        cJSON_AddBoolToObject(cue_obj, "active", true);
        cJSON_AddNumberToObject(cue_obj, "listIndex", status.cue.list_index);
        cJSON_AddNumberToObject(cue_obj, "cueIndex", status.cue.current_cue);
        cJSON_AddNumberToObject(cue_obj, "nextCueIndex", status.cue.next_cue);
        const char* c_state = "stopped";
        if (status.cue.state == 1) c_state = "holding";
        else if (status.cue.state == 2) c_state = "fading";
        else if (status.cue.state == 3) c_state = "paused";
        cJSON_AddStringToObject(cue_obj, "state", c_state);
        cJSON_AddNumberToObject(cue_obj, "generation", status.cue.generation);
        cJSON_AddNumberToObject(cue_obj, "elapsedMs", status.cue.paused_elapsed_ms);
        cJSON_AddNumberToObject(cue_obj, "manualFadeQ15", status.cue.manual_fade_q15);
        cJSON_AddItemToObject(response, "cue", cue_obj);
    } else {
        cJSON_AddNullToObject(response, "cue");
    }

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

#include "dashboard_assets.hpp"

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

FactoryApi::FactoryApi(FactoryRunner& runner) : runner_(runner) {
    runner_.set_command_journal(&command_journal_);
}


// ── Phase 4: OTA & Config Export/Import ──

esp_err_t post_ota(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Ota);
    uint32_t job_id = 0;
    esp_err_t job_err = api_ptr->job_manager().start_job(JobType::Ota, &job_id);
    if (job_err != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, job_err);
        return send_error(request, "409 Conflict", "job_already_running");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Preparing, 5);

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        api_ptr->job_manager().fail_job(job_id, ESP_ERR_NOT_FOUND, "No OTA partition found");
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_NOT_FOUND);
        return send_error(request, "500 Internal Server Error", "No OTA partition found");
    }

    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "esp_ota_begin failed (%s)", esp_err_to_name(err));
        api_ptr->job_manager().fail_job(job_id, err, "OTA begin failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "OTA begin failed");
    }

    char buf[1024];
    int received = 0;
    int remaining = request->content_len;
    int total = (remaining > 0) ? remaining : 1;

    while (remaining > 0) {
        if ((received = httpd_req_recv(request, buf, std::min(remaining, (int)sizeof(buf)))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            esp_ota_abort(update_handle);
            api_ptr->job_manager().fail_job(job_id, ESP_FAIL, "OTA receive failed");
            api_ptr->command_journal().failed(cmd_id, ESP_FAIL);
            return send_error(request, "500 Internal Server Error", "OTA receive failed");
        }
        err = esp_ota_write(update_handle, buf, received);
        if (err != ESP_OK) {
            esp_ota_abort(update_handle);
            api_ptr->job_manager().fail_job(job_id, err, "OTA write failed");
            api_ptr->command_journal().failed(cmd_id, err);
            return send_error(request, "500 Internal Server Error", "OTA write failed");
        }
        remaining -= received;
        uint8_t pct = static_cast<uint8_t>(std::min(90, 5 + (int)(((float)(total - remaining) / total) * 85)));
        api_ptr->job_manager().update_progress(job_id, JobState::Running, pct);
    }

    api_ptr->job_manager().update_progress(job_id, JobState::Verifying, 95);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        api_ptr->job_manager().fail_job(job_id, err, "OTA end failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "OTA end failed");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        api_ptr->job_manager().fail_job(job_id, err, "OTA set boot failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "OTA set boot failed");
    }

    api_ptr->job_manager().complete_job(job_id);
    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    char resp[160];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"success\",\"accepted\":true,\"commandId\":%" PRIu32 ",\"jobId\":%" PRIu32 ",\"message\":\"OTA update successful, rebooting...\"}",
        cmd_id, job_id);
    send_json(request, resp);
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
    cJSON_AddNumberToObject(root, "version", 3);
    
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

    // Auth status (provisioned only, no secrets/hashes/salts)
    bool is_prov = false;
    auto* auth_mgr = api::get_auth_manager();
    if (auth_mgr) {
        is_prov = auth_mgr->is_provisioned();
    }
    cJSON* auth_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(auth_json, "provisioned", is_prov);
    cJSON_AddItemToObject(cfg_json, "auth", auth_json);

    // Channels
    cJSON* channels_arr = cJSON_CreateArray();
    for (uint8_t c = 0; c < 16; ++c) {
        const auto& ch = cfg.channels[c];
        cJSON* cj = cJSON_CreateObject();
        cJSON_AddNumberToObject(cj, "index", c);
        cJSON_AddStringToObject(cj, "name", ch.name);
        cJSON_AddBoolToObject(cj, "enabled", ch.enabled);
        cJSON_AddNumberToObject(cj, "addressableType", static_cast<int>(ch.addressable_type));
        cJSON_AddNumberToObject(cj, "pixelOrder", static_cast<int>(ch.pixel_order));
        cJSON_AddNumberToObject(cj, "pixelCount", ch.pixel_count);
        cJSON_AddNumberToObject(cj, "maxBrightness", ch.max_brightness);
        cJSON_AddBoolToObject(cj, "gammaCorrection", ch.gamma_correction);
        cJSON_AddNumberToObject(cj, "maxMilliwatts", ch.max_milliwatts);
        cJSON_AddNumberToObject(cj, "dmxUniverse", ch.dmx_universe);
        cJSON_AddNumberToObject(cj, "dmxStartChannel", ch.dmx_start_channel);
        cJSON_AddNumberToObject(cj, "dmxMode", ch.dmx_mode);
        cJSON_AddItemToArray(channels_arr, cj);
    }
    cJSON_AddItemToObject(cfg_json, "channels", channels_arr);

    // Groups
    cJSON* groups_arr = cJSON_CreateArray();
    for (uint8_t g = 0; g < 8; ++g) {
        const auto& grp = cfg.groups[g];
        cJSON* gj = cJSON_CreateObject();
        cJSON_AddNumberToObject(gj, "index", g);
        cJSON_AddStringToObject(gj, "name", grp.name);
        cJSON_AddNumberToObject(gj, "channelMask", grp.channel_mask);
        cJSON_AddStringToObject(gj, "role", zone_role_name(grp.role));
        cJSON_AddBoolToObject(gj, "enabled", grp.enabled);
        cJSON_AddItemToArray(groups_arr, gj);
    }
    cJSON_AddItemToObject(cfg_json, "groups", groups_arr);

    // Zones (compatibility mirror)
    cJSON* zones_arr = cJSON_CreateArray();
    for (uint8_t z = 0; z < 8; ++z) {
        cJSON* zj = cJSON_CreateObject();
        cJSON_AddStringToObject(zj, "name", cfg.groups[z].name);
        cJSON_AddStringToObject(zj, "role", zone_role_name(cfg.groups[z].role));
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

            cJSON* ch_arr = cJSON_CreateArray();
            for (uint8_t c = 0; c < 16; ++c) {
                cJSON* c_obj = cJSON_CreateObject();
                cJSON* l_arr = cJSON_CreateArray();
                for (uint8_t l = 0; l < 3; ++l) {
                    const auto& layer = sc.channels[c].layers[l];
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
                cJSON_AddItemToObject(c_obj, "layers", l_arr);
                cJSON_AddItemToArray(ch_arr, c_obj);
            }
            cJSON_AddItemToObject(s, "channels", ch_arr);

            // Legacy zones mirror
            cJSON* z_arr = cJSON_CreateArray();
            for (uint8_t z = 0; z < 8; ++z) {
                cJSON* z_obj = cJSON_CreateObject();
                cJSON* l_arr = cJSON_CreateArray();
                for (uint8_t l = 0; l < 3; ++l) {
                    const auto& layer = sc.channels[z].layers[l];
                    cJSON* l_obj = cJSON_CreateObject();
                    cJSON_AddBoolToObject(l_obj, "enabled", layer.enabled);
                    cJSON_AddNumberToObject(l_obj, "effect", static_cast<int>(layer.effect));
                    cJSON_AddNumberToObject(l_obj, "speedMs", layer.speed_ms);
                    cJSON_AddItemToArray(l_arr, l_obj);
                }
                cJSON_AddItemToObject(z_obj, "layers", l_arr);
                cJSON_AddItemToArray(z_arr, z_obj);
            }
            cJSON_AddItemToObject(s, "zones", z_arr);

            // Legacy v1 proxy fields
            cJSON_AddNumberToObject(s, "effect", sc.channels[0].layers[0].effect);
            cJSON_AddNumberToObject(s, "speedMs", sc.channels[0].layers[0].speed_ms);
            
            cJSON_AddItemToArray(presets_arr, s);
        }
    }
    cJSON_AddItemToObject(root, "presets", presets_arr);

    // Scene motion
    cJSON* motion_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < lg::kMaxPresets; ++i) {
        SceneMotionConfig sm;
        if (api_ptr->config()->load_scene_motion(i, sm) == ESP_OK && (sm.track_count > 0 || sm.transition_curve != TransitionCurve::EaseInOut)) {
            cJSON* m_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(m_obj, "index", i);
            cJSON_AddStringToObject(m_obj, "transitionCurve", transition_curve_name(sm.transition_curve));
            cJSON_AddNumberToObject(m_obj, "trackCount", sm.track_count);
            cJSON* tracks = cJSON_CreateArray();
            for (uint8_t t = 0; t < sm.track_count && t < kAutomationTracksPerScene; ++t) {
                const auto& tr = sm.tracks[t];
                cJSON* tobj = cJSON_CreateObject();
                cJSON_AddStringToObject(tobj, "target", automation_target_name(tr.target));
                cJSON_AddNumberToObject(tobj, "channel", tr.channel);
                cJSON_AddNumberToObject(tobj, "zone", tr.channel); // deprecated compatibility alias
                cJSON_AddNumberToObject(tobj, "layer", tr.layer);
                cJSON_AddStringToObject(tobj, "curve", automation_curve_name(tr.curve));
                cJSON_AddBoolToObject(tobj, "loop", tr.loop);
                cJSON_AddNumberToObject(tobj, "periodMs", tr.period_ms);
                cJSON* pts = cJSON_CreateArray();
                for (uint8_t p = 0; p < tr.point_count && p < kAutomationPointsPerTrack; ++p) {
                    cJSON* pobj = cJSON_CreateObject();
                    cJSON_AddNumberToObject(pobj, "position", tr.points[p].position);
                    cJSON_AddNumberToObject(pobj, "value", tr.points[p].value);
                    cJSON_AddItemToArray(pts, pobj);
                }
                cJSON_AddItemToObject(tobj, "points", pts);
                cJSON_AddItemToArray(tracks, tobj);
            }
            cJSON_AddItemToObject(m_obj, "tracks", tracks);
            cJSON_AddItemToArray(motion_arr, m_obj);
        }
    }
    cJSON_AddItemToObject(root, "scene_motion", motion_arr);

    // Cue lists
    cJSON* cue_lists_arr = cJSON_CreateArray();
    for (uint8_t i = 0; i < lg::kMaxCueLists; ++i) {
        CueList cl;
        if (api_ptr->config()->load_cue_list(i, cl) == ESP_OK && cl.count > 0) {
            cJSON* cl_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(cl_obj, "index", i);
            cJSON_AddStringToObject(cl_obj, "name", cl.name);
            cJSON_AddStringToObject(cl_obj, "mode", cue_run_mode_name(cl.mode));
            cJSON_AddNumberToObject(cl_obj, "endAction", cl.end_action);
            cJSON* cues = cJSON_CreateArray();
            for (uint8_t c = 0; c < cl.count && c < kMaxCuesPerList; ++c) {
                cJSON* cobj = cJSON_CreateObject();
                cJSON_AddNumberToObject(cobj, "sceneIndex", cl.cues[c].scene_index);
                cJSON_AddNumberToObject(cobj, "holdMs", cl.cues[c].hold_ms);
                cJSON_AddNumberToObject(cobj, "fadeMs", cl.cues[c].fade_ms);
                cJSON_AddStringToObject(cobj, "curve", transition_curve_name(cl.cues[c].curve));
                cJSON_AddStringToObject(cobj, "label", cl.cues[c].label);
                cJSON_AddItemToArray(cues, cobj);
            }
            cJSON_AddItemToObject(cl_obj, "cues", cues);
            cJSON_AddItemToArray(cue_lists_arr, cl_obj);
        }
    }
    cJSON_AddItemToObject(root, "cue_lists", cue_lists_arr);

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

        // Explicitly ignore legacy auth secrets during restore (Requirement 21)

        cJSON* ch_arr = cJSON_GetObjectItem(cfg_json, "channels");
        if (ch_arr && cJSON_IsArray(ch_arr)) {
            int c_idx = 0;
            cJSON* cj;
            cJSON_ArrayForEach(cj, ch_arr) {
                if (c_idx >= 16) break;
                lg::json::get_string(cj, "name", cfg.channels[c_idx].name, sizeof(cfg.channels[c_idx].name));
                lg::json::get_bool(cj, "enabled", cfg.channels[c_idx].enabled);
                int atype = 0;
                if (lg::json::get_number(cj, "addressableType", atype)) cfg.channels[c_idx].addressable_type = static_cast<AddressableType>(atype);
                int pord = 0;
                if (lg::json::get_number(cj, "pixelOrder", pord)) cfg.channels[c_idx].pixel_order = static_cast<PixelOrder>(pord);
                lg::json::get_number(cj, "pixelCount", cfg.channels[c_idx].pixel_count);
                lg::json::get_number(cj, "maxBrightness", cfg.channels[c_idx].max_brightness);
                lg::json::get_bool(cj, "gammaCorrection", cfg.channels[c_idx].gamma_correction);
                lg::json::get_number(cj, "maxMilliwatts", cfg.channels[c_idx].max_milliwatts);
                lg::json::get_number(cj, "dmxUniverse", cfg.channels[c_idx].dmx_universe);
                lg::json::get_number(cj, "dmxStartChannel", cfg.channels[c_idx].dmx_start_channel);
                lg::json::get_number(cj, "dmxMode", cfg.channels[c_idx].dmx_mode);
                c_idx++;
            }
        }
        cJSON* grp_arr = cJSON_GetObjectItem(cfg_json, "groups");
        if (grp_arr && cJSON_IsArray(grp_arr)) {
            int g_idx = 0;
            cJSON* gj;
            cJSON_ArrayForEach(gj, grp_arr) {
                if (g_idx >= 8) break;
                char role_buf[32] = "";
                lg::json::get_string(gj, "name", cfg.groups[g_idx].name, sizeof(cfg.groups[g_idx].name));
                lg::json::get_number(gj, "channelMask", cfg.groups[g_idx].channel_mask);
                lg::json::get_bool(gj, "enabled", cfg.groups[g_idx].enabled);
                if (lg::json::get_string(gj, "role", role_buf, sizeof(role_buf))) {
                    parse_zone_role(role_buf, cfg.groups[g_idx].role);
                }
                g_idx++;
            }
        } else {
            cJSON* z_arr = cJSON_GetObjectItem(cfg_json, "zones");
            if (z_arr && cJSON_IsArray(z_arr)) {
                int z_idx = 0;
                cJSON* zj;
                cJSON_ArrayForEach(zj, z_arr) {
                    if (z_idx >= 8) break;
                    char role_buf[32] = "";
                    lg::json::get_string(zj, "name", cfg.groups[z_idx].name, sizeof(cfg.groups[z_idx].name));
                    if (lg::json::get_string(zj, "role", role_buf, sizeof(role_buf))) {
                        parse_zone_role(role_buf, cfg.groups[z_idx].role);
                    }
                    z_idx++;
                }
            }
        }
        
        api_ptr->config()->save_config(cfg);
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

                cJSON* c_arr = cJSON_GetObjectItem(p, "channels");
                if (c_arr && cJSON_IsArray(c_arr)) {
                    int c_idx = 0;
                    cJSON* cj;
                    cJSON_ArrayForEach(cj, c_arr) {
                        if (c_idx >= 16) break;
                        cJSON* l_arr = cJSON_GetObjectItem(cj, "layers");
                        if (l_arr && cJSON_IsArray(l_arr)) {
                            int l_idx = 0;
                            cJSON* lj;
                            cJSON_ArrayForEach(lj, l_arr) {
                                if (l_idx >= 3) break;
                                auto& layer = sc.channels[c_idx].layers[l_idx];
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
                        c_idx++;
                    }
                } else {
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
                                    auto& layer = sc.channels[z_idx].layers[l_idx];
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
                        lg::json::get_number(p, "effect", sc.channels[0].layers[0].effect);
                        lg::json::get_number(p, "speedMs", sc.channels[0].layers[0].speed_ms);
                    }
                }
                
                api_ptr->config()->save_scene(idx->valueint, sc);
            }
        }
    }

    cJSON* motion_arr = cJSON_GetObjectItem(root, "scene_motion");
    if (motion_arr && cJSON_IsArray(motion_arr)) {
        cJSON* m;
        cJSON_ArrayForEach(m, motion_arr) {
            cJSON* idx = cJSON_GetObjectItem(m, "index");
            if (idx && idx->valueint >= 0 && idx->valueint < lg::kMaxPresets) {
                SceneMotionConfig sm{};
                sm.schema_version = 1;
                cJSON* cur_j = cJSON_GetObjectItem(m, "transitionCurve");
                if (cur_j && cJSON_IsString(cur_j)) parse_transition_curve(cur_j->valuestring, sm.transition_curve);
                cJSON* trs = cJSON_GetObjectItem(m, "tracks");
                if (trs && cJSON_IsArray(trs)) {
                    uint8_t t_idx = 0;
                    cJSON* t_item;
                    cJSON_ArrayForEach(t_item, trs) {
                        if (t_idx >= kAutomationTracksPerScene) break;
                        auto& tr = sm.tracks[t_idx];
                        cJSON* target_j = cJSON_GetObjectItem(t_item, "target");
                        if (target_j && cJSON_IsString(target_j)) parse_automation_target(target_j->valuestring, tr.target);
                        cJSON* ch_j = cJSON_GetObjectItem(t_item, "channel");
                        cJSON* z_j = cJSON_GetObjectItem(t_item, "zone");
                        if (ch_j && cJSON_IsNumber(ch_j)) {
                            tr.channel = std::min<uint8_t>(15, std::max<int>(0, ch_j->valueint));
                        } else if (z_j && cJSON_IsNumber(z_j)) {
                            tr.channel = std::min<uint8_t>(7, std::max<int>(0, z_j->valueint));
                        }
                        cJSON* l_j = cJSON_GetObjectItem(t_item, "layer");
                        if (l_j && cJSON_IsNumber(l_j)) {
                            tr.layer = std::min<uint8_t>(2, std::max<int>(0, l_j->valueint));
                        }
                        cJSON* c_j = cJSON_GetObjectItem(t_item, "curve");
                        if (c_j && cJSON_IsString(c_j)) parse_automation_curve(c_j->valuestring, tr.curve);
                        cJSON* loop_j = cJSON_GetObjectItem(t_item, "loop");
                        if (loop_j && cJSON_IsBool(loop_j)) tr.loop = cJSON_IsTrue(loop_j);
                        cJSON* per_j = cJSON_GetObjectItem(t_item, "periodMs");
                        if (per_j && cJSON_IsNumber(per_j)) tr.period_ms = per_j->valueint;
                        cJSON* pts = cJSON_GetObjectItem(t_item, "points");
                        if (pts && cJSON_IsArray(pts)) {
                            uint8_t p_idx = 0;
                            cJSON* p_item;
                            cJSON_ArrayForEach(p_item, pts) {
                                if (p_idx >= kAutomationPointsPerTrack) break;
                                cJSON* pos_j = cJSON_GetObjectItem(p_item, "position");
                                cJSON* val_j = cJSON_GetObjectItem(p_item, "value");
                                if (pos_j && val_j) {
                                    tr.points[p_idx].position = pos_j->valueint;
                                    tr.points[p_idx].value = val_j->valueint;
                                    p_idx++;
                                }
                            }
                            tr.point_count = p_idx;
                        }
                        t_idx++;
                    }
                    sm.track_count = t_idx;
                }
                api_ptr->config()->save_scene_motion(idx->valueint, sm);
            }
        }
    }

    cJSON* cue_lists_arr = cJSON_GetObjectItem(root, "cue_lists");
    if (cue_lists_arr && cJSON_IsArray(cue_lists_arr)) {
        cJSON* cl_item;
        cJSON_ArrayForEach(cl_item, cue_lists_arr) {
            cJSON* idx = cJSON_GetObjectItem(cl_item, "index");
            if (idx && idx->valueint >= 0 && idx->valueint < lg::kMaxCueLists) {
                CueList cl{};
                cJSON* name_j = cJSON_GetObjectItem(cl_item, "name");
                if (name_j && cJSON_IsString(name_j)) strncpy(cl.name, name_j->valuestring, sizeof(cl.name) - 1);
                cJSON* mode_j = cJSON_GetObjectItem(cl_item, "mode");
                if (mode_j && cJSON_IsString(mode_j)) parse_cue_run_mode(mode_j->valuestring, cl.mode);
                cJSON* ea_j = cJSON_GetObjectItem(cl_item, "endAction");
                if (ea_j && cJSON_IsNumber(ea_j)) cl.end_action = ea_j->valueint;
                cJSON* cues = cJSON_GetObjectItem(cl_item, "cues");
                if (cues && cJSON_IsArray(cues)) {
                    uint8_t c_idx = 0;
                    cJSON* c_item;
                    cJSON_ArrayForEach(c_item, cues) {
                        if (c_idx >= kMaxCuesPerList) break;
                        cJSON* sc_j = cJSON_GetObjectItem(c_item, "sceneIndex");
                        if (sc_j && cJSON_IsNumber(sc_j)) cl.cues[c_idx].scene_index = sc_j->valueint;
                        cJSON* hold_j = cJSON_GetObjectItem(c_item, "holdMs");
                        if (hold_j && cJSON_IsNumber(hold_j)) cl.cues[c_idx].hold_ms = hold_j->valueint;
                        cJSON* fade_j = cJSON_GetObjectItem(c_item, "fadeMs");
                        if (fade_j && cJSON_IsNumber(fade_j)) cl.cues[c_idx].fade_ms = fade_j->valueint;
                        cJSON* cur_j = cJSON_GetObjectItem(c_item, "curve");
                        if (cur_j && cJSON_IsString(cur_j)) parse_transition_curve(cur_j->valuestring, cl.cues[c_idx].curve);
                        cJSON* lbl_j = cJSON_GetObjectItem(c_item, "label");
                        if (lbl_j && cJSON_IsString(lbl_j)) strncpy(cl.cues[c_idx].label, lbl_j->valuestring, sizeof(cl.cues[c_idx].label) - 1);
                        c_idx++;
                    }
                    cl.count = c_idx;
                }
                api_ptr->config()->save_cue_list(idx->valueint, cl);
            }
        }
    }
    
    cJSON_Delete(root);

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Restore);
    uint32_t job_id = 0;
    esp_err_t job_err = api_ptr->job_manager().start_job(JobType::Restore, &job_id);
    if (job_err != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, job_err);
        return send_error(request, "409 Conflict", "job_already_running");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 50);
    api_ptr->job_manager().complete_job(job_id);
    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    api::AuditLog::instance().log(api::ApiRole::Admin, "RESTORE", "Configuration restored via API");

    char resp[160];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"success\",\"accepted\":true,\"commandId\":%" PRIu32 ",\"jobId\":%" PRIu32 ",\"message\":\"Restore successful, rebooting...\"}",
        cmd_id, job_id);
    send_json(request, resp);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

struct WsBroadcastArg {
    FactoryApi* api;
    char payload[kRuntimeSnapshotMaxBytes];
};

struct WsPreviewArg {
    FactoryApi* api;
    std::vector<uint8_t> payload;
};

static esp_err_t get_favicon(httpd_req_t* request) {
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, nullptr, 0);
}

static esp_err_t options_handler(httpd_req_t* request) {
    httpd_resp_set_status(request, "204 No Content");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key");
    httpd_resp_set_hdr(request, "Access-Control-Max-Age", "86400");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t FactoryApi::start(
    uint16_t signage_port,
    uint16_t factory_port,
    ConfigStore* config,
    LightingRuntime* signage,
    Scheduler* scheduler,
    NodeSync* node_sync,
    DmxRouter* dmx_router,
    CueRunner* cue_runner,
    ShowTransportCoordinator* show_transport
) {
    config_ = config;
    signage_ = signage;
    scheduler_ = scheduler;
    node_sync_ = node_sync;
    dmx_router_ = dmx_router;
    cue_runner_ = cue_runner;
    show_transport_ = show_transport;

    const char* dev_name = nullptr;
    DeviceConfig cfg;
    if (config_ && config_->load_config(cfg) == ESP_OK) {
        dev_name = cfg.device_name;
    }
    identity_ = get_node_identity(dev_name);

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 80;
    http_config.stack_size = 8192;
    http_config.lru_purge_enable = true;
    http_config.recv_wait_timeout = 10;
    http_config.send_wait_timeout = 10;
    http_config.server_port = signage_port;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
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
        // ── WebSocket + Dashboard + Preflight ──
        { .uri = "/ws",         .method = HTTP_GET,     .handler = ws_handler,      .user_ctx = this,
          .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr },
        { .uri = "/",           .method = HTTP_GET,     .handler = serve_dashboard, .user_ctx = this },
        { .uri = "/index.html", .method = HTTP_GET,     .handler = serve_dashboard, .user_ctx = this },
        { .uri = "/dashboard",  .method = HTTP_GET,     .handler = serve_dashboard, .user_ctx = this },
        { .uri = "/favicon.ico",.method = HTTP_GET,     .handler = get_favicon,     .user_ctx = this },
        { .uri = "/api/v1/*",   .method = HTTP_OPTIONS, .handler = options_handler, .user_ctx = nullptr },
        
        // ── Identity & Info ──
        { .uri = "/api/v1/info",              .method = HTTP_GET,  .handler = get_info,           .user_ctx = this },
        { .uri = "/api/v1/identity",          .method = HTTP_GET,  .handler = get_identity,       .user_ctx = this },
        { .uri = "/api/v1/fleet/nodes",       .method = HTTP_GET,  .handler = get_fleet_nodes,    .user_ctx = this },

        // ── Auth endpoints (LG-029) ──
        { .uri = "/api/v1/auth/status",       .method = HTTP_GET,  .handler = get_auth_status,   .user_ctx = this },
        { .uri = "/api/v1/auth/bootstrap",    .method = HTTP_POST, .handler = post_auth_bootstrap,.user_ctx = this },
        { .uri = "/api/v1/auth/session",      .method = HTTP_POST, .handler = post_auth_session,  .user_ctx = this },
        { .uri = "/api/v1/auth/session",      .method = HTTP_GET,  .handler = get_auth_session,   .user_ctx = this },
        { .uri = "/api/v1/auth/session",      .method = HTTP_DELETE,.handler = delete_auth_session,.user_ctx = this },
        { .uri = "/api/v1/auth/ws-ticket",    .method = HTTP_POST, .handler = post_auth_ws_ticket,.user_ctx = this },
        { .uri = "/api/v1/auth/credentials",  .method = HTTP_GET,  .handler = get_auth_credentials,.user_ctx = this },
        { .uri = "/api/v1/auth/credentials",  .method = HTTP_POST, .handler = post_auth_credentials,.user_ctx = this },
        { .uri = "/api/v1/auth/credentials",  .method = HTTP_DELETE,.handler = delete_auth_credentials,.user_ctx = this },

        // ── Signage endpoints ──
        { .uri = "/api/v1/capabilities",      .method = HTTP_GET,  .handler = get_capabilities,   .user_ctx = this },
        { .uri = "/api/v1/jobs/current",      .method = HTTP_GET,  .handler = get_current_job,    .user_ctx = this },
        { .uri = "/api/v1/channels",          .method = HTTP_GET,  .handler = get_channels,       .user_ctx = this },
        { .uri = "/api/v1/channel/test",      .method = HTTP_POST, .handler = post_channel_test,  .user_ctx = this },
        { .uri = "/api/v1/presets",           .method = HTTP_GET,  .handler = get_presets,      .user_ctx = this },
        { .uri = "/api/v1/presets",           .method = HTTP_PUT,  .handler = put_preset,       .user_ctx = this },
        { .uri = "/api/v1/presets",           .method = HTTP_DELETE,.handler = delete_preset,    .user_ctx = this },
        { .uri = "/api/v1/activate",          .method = HTTP_POST, .handler = post_activate,    .user_ctx = this },
        { .uri = "/api/v1/config",            .method = HTTP_GET,  .handler = get_device_config, .user_ctx = this },
        { .uri = "/api/v1/config",            .method = HTTP_PUT,  .handler = put_device_config, .user_ctx = this },
        { .uri = "/api/v1/network/config",    .method = HTTP_GET,  .handler = get_network_config, .user_ctx = this },
        { .uri = "/api/v1/network/config",    .method = HTTP_PUT,  .handler = put_network_config, .user_ctx = this },
        { .uri = "/api/v1/network/status",    .method = HTTP_GET,  .handler = get_network_status, .user_ctx = this },
        { .uri = "/api/v1/status",            .method = HTTP_GET,  .handler = get_signage_status, .user_ctx = this },
        { .uri = "/api/v1/dmx/frame",         .method = HTTP_GET,  .handler = get_dmx_frame,      .user_ctx = this },
        { .uri = "/api/v1/manual",            .method = HTTP_PUT,   .handler = put_manual,        .user_ctx = this },
        { .uri = "/api/v1/manual",            .method = HTTP_DELETE,.handler = delete_manual,     .user_ctx = this },
        { .uri = "/api/v1/effects",           .method = HTTP_GET,  .handler = get_effects,        .user_ctx = this },
        { .uri = "/api/v1/health",            .method = HTTP_GET,  .handler = get_health,         .user_ctx = this },
        { .uri = "/api/v1/logs",              .method = HTTP_GET,  .handler = get_logs,           .user_ctx = this },
        { .uri = "/api/v1/sequence",          .method = HTTP_GET,  .handler = get_sequence,     .user_ctx = this },
        { .uri = "/api/v1/sequence",          .method = HTTP_PUT,  .handler = put_sequence,     .user_ctx = this },
        { .uri = "/api/v1/schedules",         .method = HTTP_GET,  .handler = get_calendar_schedule, .user_ctx = this },
        { .uri = "/api/v1/schedules",         .method = HTTP_PUT,  .handler = put_calendar_schedule, .user_ctx = this },
        { .uri = "/api/v1/scene-motion",      .method = HTTP_GET,  .handler = get_scene_motion, .user_ctx = this },
        { .uri = "/api/v1/scene-motion",      .method = HTTP_PUT,  .handler = put_scene_motion, .user_ctx = this },
        { .uri = "/api/v1/scene-motion",      .method = HTTP_DELETE,.handler = delete_scene_motion, .user_ctx = this },
        { .uri = "/api/v1/cue-lists",         .method = HTTP_GET,  .handler = get_cue_lists,    .user_ctx = this },
        { .uri = "/api/v1/cue-list",          .method = HTTP_GET,  .handler = get_cue_list,     .user_ctx = this },
        { .uri = "/api/v1/cue-list",          .method = HTTP_PUT,  .handler = put_cue_list,     .user_ctx = this },
        { .uri = "/api/v1/cue-list",          .method = HTTP_DELETE,.handler = delete_cue_list,  .user_ctx = this },
        { .uri = "/api/v1/cue/control",       .method = HTTP_POST, .handler = post_cue_control, .user_ctx = this },
        { .uri = "/api/v1/cue/status",        .method = HTTP_GET,  .handler = get_cue_status,   .user_ctx = this },
        { .uri = "/api/v1/show/status",       .method = HTTP_GET,  .handler = get_show_status,   .user_ctx = this },
        { .uri = "/api/v1/show/control",      .method = HTTP_POST, .handler = post_show_control,  .user_ctx = this },
        { .uri = "/api/v1/sync",              .method = HTTP_GET,  .handler = get_sync_status,     .user_ctx = this },
        { .uri = "/api/v1/show-manifest",     .method = HTTP_GET,  .handler = get_show_manifest, .user_ctx = this },
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
        signage_->set_generation_applied_callback([this](uint32_t applied_gen) {
            CommandReceipt sc_receipt;
            if (this->command_journal().get_latest(CommandDomain::Scene, sc_receipt)) {
                if (sc_receipt.state == CommandState::Accepted && applied_gen >= sc_receipt.generation) {
                    this->command_journal().applied(sc_receipt.id);
                }
            }
        });

        signage_->set_preview_callback([this](const RenderFrame& frame, const DeviceConfig& cfg) {
            if (this->factory_server_ == nullptr) return;

            // Rate limit to ~10 fps (100ms)
            uint32_t now = esp_log_timestamp();
            if (now - this->last_preview_ms_.load(std::memory_order_relaxed) < 100) return;
            
            bool expected = false;
            if (!this->preview_pending_.compare_exchange_strong(expected, true)) {
                return; // Already pending
            }
            this->last_preview_ms_.store(now, std::memory_order_relaxed);

            // Periodically broadcast live status snapshot (~5 fps = 200ms) over WebSocket
            if (now - this->last_status_broadcast_ms_.load(std::memory_order_relaxed) >= 200) {
                this->last_status_broadcast_ms_.store(now, std::memory_order_relaxed);
                this->notify_status_async();
            }

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
                    const auto& ch_cfg = cfg.channels[f];
                    const auto& ch_frame = frame.channels[f];
                    
                    if (ch_cfg.pixel_count > 0) {
                        uint16_t pcount = std::min<uint16_t>(ch_cfg.pixel_count, 144);
                        size_t offset = 1 + (f * 144 * 3);
                        for (uint16_t p = 0; p < pcount; ++p) {
                            const auto& lv = ch_frame.pixels[p];
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

    if (httpd_get_client_list(msg->api->factory_server_, &clients, client_fds.data()) != ESP_OK || clients == 0) {
        msg->api->status_pending_.store(false);
        return;
    }

    size_t ws_clients = 0;
    for (size_t i = 0; i < clients; ++i) {
        if (httpd_ws_get_fd_info(msg->api->factory_server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            ws_clients++;
        }
    }
    if (ws_clients == 0) {
        msg->api->status_pending_.store(false);
        return;
    }

    uint32_t stream_seq = msg->api->next_ws_seq();
    RuntimeSnapshot snapshot = collect_runtime_snapshot(msg->api, stream_seq);

    FixedJsonWriter writer(msg->payload, sizeof(msg->payload));
    if (!serialize_runtime_snapshot(snapshot, writer)) {
        msg->api->status_pending_.store(false);
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(msg->payload);
    frame.len = writer.size();

    for (size_t i = 0; i < clients; ++i) {
        if (httpd_ws_get_fd_info(msg->api->factory_server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(msg->api->factory_server_, client_fds[i], &frame);
        }
    }

    msg->api->status_pending_.store(false);
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

    bool expected = false;
    if (!status_pending_.compare_exchange_strong(expected, true)) {
        return; // Another status job is already queued/running
    }

    auto* arg = new (std::nothrow) WsBroadcastArg{};
    if (!arg) {
        status_pending_.store(false);
        return;
    }
    arg->api = this;
    arg->payload[0] = '\0';

    if (httpd_queue_work(factory_server_, ws_broadcast_work, arg) != ESP_OK) {
        status_pending_.store(false);
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
