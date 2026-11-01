// components/factory/FactoryApi.cpp
#include "FactoryApi.hpp"
#include "RuntimeSnapshot.hpp"
#include "RuntimeMemory.hpp"
#include "RuntimeServices.hpp"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "OtaBootVerifier.hpp"
#include "StorageContract.hpp"
#include "OtaImagePreflight.hpp"
#include "esp_chip_info.h"
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
#include "PowerConfigLimits.hpp"
#include "ChannelHardware.hpp"
#include "BoardOutputSafety.hpp"
#include "ResetForensics.hpp"
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
#include "CertificationService.hpp"
#include "CertificationPlan.hpp"
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
// A current scene contains 16 channels x 3 layers, plus bounded motion tracks.
// Keep other API bodies capped at 8 KiB; JSON heap admission still applies.
constexpr size_t kMaxPresetBodyBytes = 16384;


esp_err_t send_json(httpd_req_t* request,
                    const char* body,
                    const char* status = "200 OK") {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    return httpd_resp_sendstr(request, body);
}

esp_err_t send_json(httpd_req_t* request, cJSON* json, const char* status = "200 OK") {
    char* body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) { note_api_allocation_failure(); return ESP_FAIL; }
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

// LG-034: structured error body with extra fields (e.g. cueIndex, currentChecksum).
esp_err_t send_error_json(httpd_req_t* request, const char* status, cJSON* body) {
    esp_err_t res = send_json(request, body, status);
    return res;
}

esp_err_t send_memory_admission_error(httpd_req_t* request, uint32_t required) {
    char body[192];
    snprintf(body, sizeof(body),
             "{\"error\":\"insufficient_runtime_memory\",\"requiredBytes\":%u,\"freeBytes\":%u,\"reserveBytes\":%u}",
             (unsigned)required, (unsigned)capture_runtime_memory().free_8bit,
             (unsigned)kPostTopologyHeapReserveBytes);
    return send_json(request, body, "507 Insufficient Storage");
}

esp_err_t send_output_admission_error(httpd_req_t* request, const OutputMemoryAdmission& adm) {
    char body[256];
    snprintf(body, sizeof(body),
             "{\"error\":\"insufficient_runtime_memory\",\"reason\":\"%s\",\"requiredGeneric\":%u,\"requiredInternal\":%u,\"free8\":%u,\"freeInternal\":%u,\"largestInternal\":%u,\"reserveGeneric\":%u,\"reserveInternal\":%u}",
             adm.rejection_reason ? adm.rejection_reason : "unknown",
             (unsigned)adm.generic_required, (unsigned)adm.internal_required,
             (unsigned)adm.free_8bit, (unsigned)adm.free_internal,
             (unsigned)adm.largest_internal,
             (unsigned)adm.generic_reserve, (unsigned)adm.internal_reserve);
    return send_json(request, body, "507 Insufficient Storage");
}

#include <cmath>

// Strict UTF-8 byte length for API limits (names/labels).
static size_t utf8_byte_len(const char* s) {
    return s ? strlen(s) : 0;
}

// Rejects overlong sequences, surrogates and truncated multi-byte tails.
static bool is_valid_utf8(const char* s) {
    if (!s) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        int need = 0;
        uint32_t cp = 0;
        if ((*p & 0xE0) == 0xC0) { need = 1; cp = *p & 0x1Fu; }
        else if ((*p & 0xF0) == 0xE0) { need = 2; cp = *p & 0x0Fu; }
        else if ((*p & 0xF8) == 0xF0) { need = 3; cp = *p & 0x07u; }
        else return false;
        p++;
        while (need-- > 0) {
            if ((*p & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (*p & 0x3Fu);
            p++;
        }
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    }
    return true;
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

// Count the cJSON value nodes before allocating its tree. Object colons and
// array elements each own one node; punctuation inside strings is ignored.
// Strings cannot expand beyond their encoded input plus allocator overhead.
static cJSON* parse_admitted_json(const char* body, size_t length) {
    if (!body || std::memchr(body, '\0', length)) return nullptr;
    const auto budget = json_scratch_budget(body, length, sizeof(cJSON));
    if (!budget.bounded) return nullptr;
    if (!check_memory_admission_snapshot(capture_runtime_memory(), budget.scratch_bytes,
                                        kPostTopologyHeapReserveBytes, length + 1).accepted) {
        note_api_allocation_failure();
        return nullptr;
    }
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root) return nullptr;
    // cJSON otherwise accepts a valid prefix followed by unrelated bytes.
    // Every API body and backup record is one complete JSON object.
    while (end && end < body + length &&
           (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if (!cJSON_IsObject(root) || end != body + length) {
        cJSON_Delete(root);
        return nullptr;
    }
    return root;
}

esp_err_t read_json(httpd_req_t* request, cJSON** out, size_t max_body_bytes = kMaxBodyBytes) {
    if (request->content_len <= 0 ||
        request->content_len > max_body_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    std::unique_ptr<char[]> body(new (std::nothrow) char[request->content_len + 1]());
    if (!body) { note_api_allocation_failure(); return ESP_ERR_NO_MEM; }

    int received = 0;
    unsigned timeouts = 0;

    while (received < request->content_len) {
        const int result = httpd_req_recv(
            request,
            body.get() + received,
            request->content_len - received
        );

        if (result == HTTPD_SOCK_ERR_TIMEOUT) { if (++timeouts >= 3) return ESP_ERR_TIMEOUT; continue; }
        if (result <= 0) return ESP_FAIL;

        received += result;
    }

    body[received] = '\0';
    *out = parse_admitted_json(body.get(), received);

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
    } else if (strcmp(value, "stress_cycle") == 0 || strcmp(value, "burn_in") == 0) {
        effect = FactoryEffect::StressCycle;
    } else if (strcmp(value, "hardware_coexistence") == 0 || strcmp(value, "coexistence") == 0) {
        effect = FactoryEffect::HardwareCoexistence;
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
    const uint32_t current_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    uint32_t elapsed_ms = 0;
    uint32_t remaining_ms = 0;
    const char* state_str = "idle";

    if (status.test_active) {
        elapsed_ms = (current_ms >= status.effect_started_ms) ? (current_ms - status.effect_started_ms) : 0;
        remaining_ms = (status.effect_duration_ms > elapsed_ms) ? (status.effect_duration_ms - elapsed_ms) : 0;
        state_str = status.auto_loop ? "looping" : "running";
    } else if (status.session.state == FactoryTestState::WaitingForOperator) {
        state_str = "waiting_for_operator";
    }

    char session_buf[384];
    if (status.session.session_id != 0) {
        snprintf(
            session_buf,
            sizeof(session_buf),
            "{\"id\":%" PRIu32 ",\"kind\":\"%s\",\"state\":\"%s\",\"channel\":%d,\"startedMs\":%" PRIu64 ",\"finishedMs\":%" PRIu64 ",\"operatorConfirmationRequired\":%s,\"errorCode\":%" PRId32 ",\"stage\":\"%s\",\"stageIndex\":%u,\"stageCount\":%u}",
            status.session.session_id,
            test_kind_name(status.session.kind),
            test_state_name(status.session.state),
            status.session.channel == 0xFF ? -1 : static_cast<int>(status.session.channel),
            status.session.started_ms,
            status.session.finished_ms,
            status.session.operator_confirmation_required ? "true" : "false",
            status.session.error_code,
            test_stage_name(status.session.stage),
            status.session.stage_index,
            status.session.stage_count
        );
    } else {
        snprintf(session_buf, sizeof(session_buf), "null");
    }

    snprintf(
        response,
        max_len,
        "{\"state\":\"%s\",\"effect\":\"%s\",\"startedMs\":%" PRIu32 ",\"effectStartedMs\":%" PRIu32
        ",\"durationMs\":%" PRIu32 ",\"elapsedMs\":%" PRIu32 ",\"remainingMs\":%" PRIu32
        ",\"brightness\":%u,\"autoLoop\":%s,\"outputsEnabled\":%s"
        ",\"testActive\":%s,\"commandRejections\":%" PRIu32 ",\"commitFailures\":%" PRIu32
        ",\"activeChannel\":%d,\"session\":%s}",
        state_str,
        effect_name(status.active_effect),
        status.effect_started_ms,
        status.effect_started_ms,
        status.effect_duration_ms,
        elapsed_ms,
        remaining_ms,
        status.brightness,
        status.auto_loop ? "true" : "false",
        status.outputs_enabled ? "true" : "false",
        status.test_active ? "true" : "false",
        status.command_rejections,
        status.commit_failures,
        status.active_channel == 0xFF ? -1 : static_cast<int>(status.active_channel),
        session_buf
    );
}

esp_err_t get_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    const FactoryStatus status = api_ptr->runner().status();

    char response[768];
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
    FactoryCommand command{};
    command.command_id = cmd_id;
    command.type = FactoryCommandType::Abort;
    if (!api_ptr->runner().enqueue(command)) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_STATE);
        return send_error(request, "409 Conflict", "command_queue_full");
    }

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"accepted\":true,\"commandId\":%" PRIu32 ",\"action\":\"blackout\",\"cancellationRequested\":true}", cmd_id);
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

esp_err_t post_factory_test_start(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    auto* certification = api_ptr->certification_service();
    if (certification && (certification->is_in_progress() || certification->cancellation_pending())) {
        return send_error(request, "409 Conflict", "certification_session_active");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    const cJSON* kind_item = cJSON_GetObjectItemCaseSensitive(root, "kind");
    if (!cJSON_IsString(kind_item) || kind_item->valuestring == nullptr) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_or_invalid_kind");
    }

    FactoryTestKind kind;
    if (!parse_test_kind(kind_item->valuestring, kind)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "unknown_test_kind");
    }

    uint8_t channel = 0xFF;
    if (cJSON_HasObjectItem(root, "channel")) {
        const cJSON* ch_item = cJSON_GetObjectItemCaseSensitive(root, "channel");
        if (cJSON_IsNumber(ch_item)) {
            channel = static_cast<uint8_t>(ch_item->valueint);
        }
    }

    uint32_t duration_ms = 5000;
    if (cJSON_HasObjectItem(root, "durationMs")) {
        json_u32(root, "durationMs", 100, 86400000, duration_ms);
    }

    uint8_t brightness = 32;
    if (cJSON_HasObjectItem(root, "brightness")) {
        json_u8(root, "brightness", 1, 96, brightness);
    }

    cJSON_Delete(root);

    uint32_t session_id = 0;
    if (!api_ptr->runner().start_test_session(kind, channel, duration_ms, brightness, &session_id)) {
        return send_error(request, "409 Conflict", "failed_to_start_session");
    }

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"accepted\":true,\"sessionId\":%" PRIu32 ",\"kind\":\"%s\",\"channel\":%d}",
             session_id, test_kind_name(kind), channel == 0xFF ? -1 : static_cast<int>(channel));
    return send_json(request, resp, "202 Accepted");
}

esp_err_t post_factory_test_confirm(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint32_t session_id = 0;
    if (!json_u32(root, "sessionId", 1, UINT32_MAX, session_id)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_or_invalid_session_id");
    }

    const cJSON* passed_item = cJSON_GetObjectItemCaseSensitive(root, "passed");
    if (!cJSON_IsBool(passed_item)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_or_invalid_passed");
    }
    const bool passed = cJSON_IsTrue(passed_item);

    const cJSON* notes_item = cJSON_GetObjectItemCaseSensitive(root, "notes");
    const char* notes = (cJSON_IsString(notes_item) && notes_item->valuestring) ? notes_item->valuestring : nullptr;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().confirm_test_session(session_id, passed, notes)) {
        cJSON_Delete(root);
        return send_error(request, "409 Conflict", "failed_to_confirm_session");
    }

    cJSON_Delete(root);

    char resp[96];
    snprintf(resp, sizeof(resp), "{\"confirmed\":true,\"sessionId\":%" PRIu32 ",\"passed\":%s}",
             session_id, passed ? "true" : "false");
    return send_json(request, resp);
}

esp_err_t post_factory_test_abort(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().abort_test_session()) {
        return send_error(request, "409 Conflict", "command_queue_full");
    }
    return send_json(request, "{\"accepted\":true,\"cancellationRequested\":true}", "202 Accepted");
}

esp_err_t get_factory_test_report(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    const FactoryValidationReport report = api_ptr->runner().validation_report();

    cJSON* root = cJSON_CreateObject();
    cJSON* addr_arr = cJSON_CreateArray();
    for (int i = 0; i < 8; ++i) {
        cJSON* ch_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch_obj, "channel", i);
        cJSON_AddBoolToObject(ch_obj, "tested", report.addressable[i].tested);
        cJSON_AddBoolToObject(ch_obj, "passed", report.addressable[i].passed);
        cJSON_AddNumberToObject(ch_obj, "timestampMs", report.addressable[i].timestamp_ms);
        cJSON_AddItemToArray(addr_arr, ch_obj);
    }
    cJSON_AddItemToObject(root, "addressable", addr_arr);

    cJSON* pwm_arr = cJSON_CreateArray();
    for (int i = 0; i < 8; ++i) {
        cJSON* ch_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch_obj, "channel", i + 8);
        cJSON_AddBoolToObject(ch_obj, "tested", report.pwm[i].tested);
        cJSON_AddBoolToObject(ch_obj, "passed", report.pwm[i].passed);
        cJSON_AddNumberToObject(ch_obj, "timestampMs", report.pwm[i].timestamp_ms);
        cJSON_AddItemToArray(pwm_arr, ch_obj);
    }
    cJSON_AddItemToObject(root, "pwm", pwm_arr);

    cJSON* comb = cJSON_CreateObject();
    cJSON_AddBoolToObject(comb, "tested", report.combined.tested);
    cJSON_AddBoolToObject(comb, "passed", report.combined.passed);
    cJSON_AddNumberToObject(comb, "timestampMs", report.combined.timestamp_ms);
    cJSON_AddItemToObject(root, "combined", comb);

    cJSON* safety = cJSON_CreateObject();
    cJSON_AddBoolToObject(safety, "tested", report.safety_gate.tested);
    cJSON_AddBoolToObject(safety, "passed", report.safety_gate.passed);
    cJSON_AddNumberToObject(safety, "timestampMs", report.safety_gate.timestamp_ms);
    cJSON_AddItemToObject(root, "safetyGate", safety);

    cJSON* burn = cJSON_CreateObject();
    cJSON_AddBoolToObject(burn, "tested", report.burn_in.tested);
    cJSON_AddBoolToObject(burn, "passed", report.burn_in.passed);
    cJSON_AddNumberToObject(burn, "timestampMs", report.burn_in.timestamp_ms);
    cJSON_AddNumberToObject(burn, "durationS", report.burn_in_duration_s);
    cJSON_AddNumberToObject(burn, "errors", report.burn_in_errors);
    cJSON_AddItemToObject(root, "burnIn", burn);

    char* rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!rendered) {
        return send_error(request, "500 Internal Server Error", "oom");
    }

    esp_err_t res = send_json(request, rendered);
    free(rendered);
    return res;
}

esp_err_t post_factory_burn_in_start(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    auto* certification = api_ptr->certification_service();
    if (certification && (certification->is_in_progress() || certification->cancellation_pending())) {
        return send_error(request, "409 Conflict", "certification_session_active");
    }

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint32_t duration_minutes = 60;
    if (cJSON_HasObjectItem(root, "durationMinutes")) {
        json_u32(root, "durationMinutes", 1, 1440, duration_minutes);
    }

    uint8_t brightness = FactoryRunner::kBurnInDefaultBrightness;
    if (cJSON_HasObjectItem(root, "brightness")) {
        json_u8(root, "brightness", 1, FactoryRunner::kBurnInAbsoluteMaxBrightness, brightness);
    }

    cJSON_Delete(root);

    uint32_t job_id = 0;
    if (!api_ptr->runner().start_burn_in_job(&api_ptr->job_manager(), duration_minutes, brightness, &job_id)) {
        return send_error(request, "409 Conflict", "failed_to_start_burn_in");
    }

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"accepted\":true,\"jobId\":%" PRIu32 ",\"type\":\"burn_in\",\"durationMinutes\":%" PRIu32 ",\"brightness\":%u}",
             job_id, duration_minutes, brightness);
    return send_json(request, resp, "202 Accepted");
}

esp_err_t post_factory_burn_in_stop(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->runner().stop_burn_in_job()) {
        return send_error(request, "409 Conflict", "no_active_burn_in_job");
    }

    return send_json(request, "{\"accepted\":true,\"cancellationRequested\":true}", "202 Accepted");
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

static cJSON* storage_json(bool detailed = false) {
    const auto snap = capture_storage();
    cJSON* root = cJSON_CreateObject();
    if (!root) return nullptr;
    cJSON_AddNumberToObject(root, "physicalFlashBytes", snap.physical_bytes);
    cJSON_AddNumberToObject(root, "configuredFlashBytes", snap.configured_bytes);
    cJSON_AddNumberToObject(root, "physicalBytes", snap.physical_bytes);
    cJSON_AddNumberToObject(root, "configuredBytes", snap.configured_bytes);
    cJSON_AddNumberToObject(root, "flashMb", snap.physical_bytes / 1048576);
    cJSON_AddNumberToObject(root, "partitionLayoutVersion", kPartitionLayoutVersion);
    cJSON_AddNumberToObject(root, "partitionLayout", kPartitionLayoutVersion);
    cJSON_AddNumberToObject(root, "layout", kPartitionLayoutVersion);
    cJSON_AddStringToObject(root, "health", storage_health_name(snap.health));
    cJSON_AddStringToObject(root, "runningPartition", snap.running ? snap.running->label : "unknown");
    cJSON_AddNumberToObject(root, "appSlotBytes", snap.running ? snap.running->size : 0);
    cJSON_AddNumberToObject(root, "imageBytes", snap.image_bytes);
    if (detailed) {
        auto partition_json = [](const esp_partition_t* p) -> cJSON* {
            if (!p) return cJSON_CreateNull();
            cJSON* object = cJSON_CreateObject();
            cJSON_AddStringToObject(object, "label", p->label);
            cJSON_AddStringToObject(object, "type", p->type == ESP_PARTITION_TYPE_APP ? "app" : "data");
            cJSON_AddNumberToObject(object, "address", p->address);
            cJSON_AddNumberToObject(object, "size", p->size);
            return object;
        };
        cJSON_AddItemToObject(root, "running", partition_json(snap.running));
        cJSON_AddItemToObject(root, "nextOta", partition_json(snap.next));
        cJSON* parts = cJSON_AddArrayToObject(root, "partitions");
        auto iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
        while (iterator) {
            cJSON_AddItemToArray(parts, partition_json(esp_partition_get(iterator)));
            iterator = esp_partition_next(iterator);
        }
    }
    return root;
}

esp_err_t get_info(httpd_req_t* request) {
    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    DeviceConfig cfg;
    const char* dev_name = nullptr;
    if (api_ptr->config() && api_ptr->config()->load_config(cfg) == ESP_OK) {
        dev_name = cfg.device_name;
    }
    NodeIdentity id = get_node_identity(dev_name);
    cJSON* root = cJSON_CreateObject();
    if (!root) return send_error(request, "500 Internal Server Error", "json_encode_failed");
    cJSON_AddStringToObject(root, "id", id.id);
    cJSON_AddStringToObject(root, "name", id.name);
    cJSON_AddStringToObject(root, "family", id.family);
    cJSON_AddStringToObject(root, "model", id.model);
    cJSON_AddStringToObject(root, "hardwareRevision", id.hw_revision);
    cJSON_AddStringToObject(root, "firmwareVersion", id.fw_version);

    const auto reset_d = lg::board::get_reset_diagnostics();
    cJSON_AddNumberToObject(root, "bootSessionId", reset_d.boot_session_id);
    cJSON_AddStringToObject(root, "resetReason", reset_d.reason_str);
    cJSON_AddNumberToObject(root, "consecutiveAbnormalBoots", reset_d.consecutive_abnormal_boots);

    cJSON_AddItemToObject(root, "storage", storage_json());
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return send_error(request, "500 Internal Server Error", "json_encode_failed");
    const auto err = send_json(request, body);
    cJSON_free(body);
    return err;
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
        const auto power = sig->power_diagnostics();
        output.current_power_mw = power.limited_power_mw;
        output.throttling_events = power.throttling_events;
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

    // LG-038.3 §3/§45/§59: real 8-bit heap truth in health, plus health issues
    // derived from BOTH free total and largest block.
    const RuntimeMemorySnapshot mem = capture_runtime_memory();
    memory.free8 = mem.free_8bit;
    memory.largest8 = mem.largest_8bit;
    memory.minimum_free = mem.minimum_free_heap;
    memory.health = evaluate_memory_health(mem);
    memory.low_runtime_memory = (memory.health == MemoryHealth::LowRuntimeMemory ||
                                 memory.health == MemoryHealth::Critical);
    memory.heap_fragmented = (memory.health == MemoryHealth::HeapFragmented ||
                              memory.health == MemoryHealth::Critical);
}

cJSON* RuntimeHealthSnapshot::to_json() const {
    cJSON* root = cJSON_CreateObject();
    if (!root) return nullptr;

    cJSON_AddNumberToObject(root, "schema_version", kSchemaVersion);
    cJSON_AddItemToObject(root, "flash", storage_json());

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

    // LG-038.3 §3/§58/§59: memory card data. Primary metric is free8.
    cJSON* mem_obj = cJSON_AddObjectToObject(root, "memory");
    cJSON_AddNumberToObject(mem_obj, "free8", memory.free8);
    cJSON_AddNumberToObject(mem_obj, "largest8", memory.largest8);
    cJSON_AddNumberToObject(mem_obj, "minimum_free", memory.minimum_free);
    cJSON_AddStringToObject(mem_obj, "health", memory_health_name(memory.health));
    cJSON_AddBoolToObject(mem_obj, "low_runtime_memory", memory.low_runtime_memory);
    cJSON_AddBoolToObject(mem_obj, "heap_fragmented", memory.heap_fragmented);

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

esp_err_t get_diagnostics_storage(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;
    cJSON* root = storage_json(true);
    if (!root) return send_error(request, "500 Internal Server Error", "json_encode_failed");
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return send_error(request, "500 Internal Server Error", "json_encode_failed");
    const auto err = send_json(request, body);
    cJSON_free(body);
    return err;
}

esp_err_t get_diagnostics_output(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    auto* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }

    uint32_t active_lanes = 0;
    uint32_t max_pixels = AddressableDriver::kPixels;
    uint32_t last_commit_us = 0;
    uint32_t max_commit_us = 0;
    int failed_lane = -1;

    bool pwm_available = false;
    uint8_t pwm_address = 85;
    uint32_t i2c_hz = 100000;
    uint32_t pwm_hz = 1017;
    bool oe_enabled = false;
    uint32_t pwm_commit_failures = 0;

    uint32_t target_period_us = 25000;
    uint32_t last_frame_us = 0;
    uint32_t max_frame_us = 0;
    uint32_t deadline_misses = 0;

    if (api_ptr && api_ptr->signage()) {
        auto* signage = api_ptr->signage();
        const auto& ctrl = signage->controller();
        const auto commit_m = ctrl.commit_metrics();
        const auto pca_diag = ctrl.pwm().diagnostics();
        const auto frame_m = signage->frame_timing_metrics();

        for (size_t lane = 0; lane < AddressableDriver::kLanes; ++lane) {
            if (ctrl.pixels().configured(lane)) {
                active_lanes++;
            }
        }
        last_commit_us = commit_m.last_addressable_us;
        max_commit_us = commit_m.max_addressable_us;
        failed_lane = (commit_m.last_failed_lane != kNoFailedLane) ? static_cast<int>(commit_m.last_failed_lane) : -1;

        pwm_available = ctrl.is_pwm_available();
        pwm_address = pca_diag.address;
        i2c_hz = pca_diag.i2c_hz;
        pwm_hz = pca_diag.pwm_hz;
        oe_enabled = pca_diag.outputs_enabled;
        pwm_commit_failures = pca_diag.commit_failures;

        last_frame_us = frame_m.last_frame_us;
        max_frame_us = frame_m.max_frame_us;
        deadline_misses = frame_m.deadline_misses;
    }

    cJSON* addr = cJSON_CreateObject();
    cJSON_AddNumberToObject(addr, "activeLanes", active_lanes);
    cJSON_AddNumberToObject(addr, "maxPixels", max_pixels);
    cJSON_AddNumberToObject(addr, "lastCommitUs", last_commit_us);
    cJSON_AddNumberToObject(addr, "maxCommitUs", max_commit_us);
    if (failed_lane >= 0) {
        cJSON_AddNumberToObject(addr, "failedLane", failed_lane);
    } else {
        cJSON_AddNullToObject(addr, "failedLane");
    }
    cJSON_AddItemToObject(root, "addressable", addr);

    cJSON* pwm = cJSON_CreateObject();
    cJSON_AddBoolToObject(pwm, "available", pwm_available);
    cJSON_AddNumberToObject(pwm, "address", pwm_address);
    cJSON_AddNumberToObject(pwm, "i2cHz", i2c_hz);
    cJSON_AddNumberToObject(pwm, "pwmHz", pwm_hz);
    cJSON_AddBoolToObject(pwm, "oeEnabled", oe_enabled);
    cJSON_AddNumberToObject(pwm, "commitFailures", pwm_commit_failures);
    if (api_ptr && api_ptr->signage()) {
        const auto pca_d = api_ptr->signage()->controller().pwm().diagnostics();
        cJSON_AddBoolToObject(pwm, "healthy", pca_d.healthy);
        cJSON_AddBoolToObject(pwm, "recoveryRequired", pca_d.recovery_required);
        cJSON_AddStringToObject(pwm, "lastFaultKind", pca_fault_kind_name(pca_d.last_fault_kind));
        cJSON_AddNumberToObject(pwm, "transactions", pca_d.transactions);
        cJSON_AddNumberToObject(pwm, "transactionFailures", pca_d.transaction_failures);
        cJSON_AddNumberToObject(pwm, "consecutiveFailures", pca_d.consecutive_failures);
        cJSON_AddNumberToObject(pwm, "recoveryAttempts", pca_d.recovery_attempts);
        cJSON_AddNumberToObject(pwm, "recoverySuccesses", pca_d.recovery_successes);
        cJSON_AddNumberToObject(pwm, "recoveryFailures", pca_d.recovery_failures);
        cJSON_AddNumberToObject(pwm, "busClearAttempts", pca_d.bus_clear_attempts);
        cJSON_AddNumberToObject(pwm, "busClearSuccesses", pca_d.bus_clear_successes);
        cJSON_AddNumberToObject(pwm, "busClearFailures", pca_d.bus_clear_failures);
        cJSON_AddNumberToObject(pwm, "driverReinstalls", pca_d.driver_reinstalls);
        cJSON_AddBoolToObject(pwm, "lastSdaHigh", pca_d.last_sda_high);
        cJSON_AddBoolToObject(pwm, "lastSclHigh", pca_d.last_scl_high);
        cJSON_AddNumberToObject(pwm, "lastTransactionUs", pca_d.last_transaction_us);
        cJSON_AddNumberToObject(pwm, "maxTransactionUs", pca_d.max_transaction_us);
        cJSON_AddNumberToObject(pwm, "lastError", pca_d.last_error);
        cJSON_AddNumberToObject(pwm, "initCore", pca_d.init_core);
    }
    cJSON_AddItemToObject(root, "pwm", pwm);

    cJSON* rmt_obj = cJSON_CreateObject();
    if (api_ptr && api_ptr->signage()) {
        const auto rmt_diag = api_ptr->signage()->controller().rmt_diagnostics();
        cJSON_AddStringToObject(rmt_obj, "health", rmt_health_name(rmt_diag.health));
        cJSON_AddNumberToObject(rmt_obj, "core", rmt_diag.allocation_core);
        cJSON_AddNumberToObject(rmt_obj, "priority", rmt_diag.interrupt_priority);
        cJSON_AddBoolToObject(rmt_obj, "iramSafe", rmt_diag.iram_safe);
        cJSON_AddNumberToObject(rmt_obj, "activeLanes", rmt_diag.active_lanes);
        cJSON_AddNumberToObject(rmt_obj, "frames", rmt_diag.total_frames);
        cJSON_AddNumberToObject(rmt_obj, "failedFrames", rmt_diag.failed_frames);
        cJSON_AddNumberToObject(rmt_obj, "deadlineNearMisses", rmt_diag.deadline_near_misses);

        cJSON* lanes_arr = cJSON_CreateArray();
        for (size_t l = 0; l < AddressableDriver::kLanes; ++l) {
            const auto& ld = rmt_diag.lanes[l];
            cJSON* lane_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(lane_obj, "lane", l);
            cJSON_AddNumberToObject(lane_obj, "submissions", ld.submissions);
            cJSON_AddNumberToObject(lane_obj, "completed", ld.completed);
            cJSON_AddNumberToObject(lane_obj, "txDoneCallbacks", ld.tx_done_callbacks);
            cJSON_AddNumberToObject(lane_obj, "completionMismatches", ld.completion_mismatches);
            cJSON_AddNumberToObject(lane_obj, "symbolMismatches", ld.symbol_count_mismatches);
            cJSON_AddNumberToObject(lane_obj, "lastExpectedSymbols", ld.last_expected_symbols);
            cJSON_AddNumberToObject(lane_obj, "lastActualSymbols", ld.last_actual_symbols);
            cJSON_AddNumberToObject(lane_obj, "submitFailures", ld.submit_failures);
            cJSON_AddNumberToObject(lane_obj, "waitTimeouts", ld.wait_timeouts);
            cJSON_AddNumberToObject(lane_obj, "otherFailures", ld.other_failures);
            cJSON_AddNumberToObject(lane_obj, "deadlineNearMisses", ld.deadline_near_misses);
            cJSON_AddNumberToObject(lane_obj, "maxSubmitUs", ld.max_submit_us);
            cJSON_AddNumberToObject(lane_obj, "maxWaitUs", ld.max_wait_us);
            cJSON_AddNumberToObject(lane_obj, "maxWaitOverWireUs", ld.max_wait_over_wire_us);
            cJSON_AddNumberToObject(lane_obj, "lastError", ld.last_error);
            cJSON_AddItemToArray(lanes_arr, lane_obj);
        }
        cJSON_AddItemToObject(rmt_obj, "lanes", lanes_arr);
    }
    cJSON_AddItemToObject(root, "rmt", rmt_obj);

    cJSON* frame = cJSON_CreateObject();
    cJSON_AddNumberToObject(frame, "targetPeriodUs", target_period_us);
    cJSON_AddNumberToObject(frame, "lastUs", last_frame_us);
    cJSON_AddNumberToObject(frame, "maxUs", max_frame_us);
    cJSON_AddNumberToObject(frame, "deadlineMisses", deadline_misses);
    cJSON_AddItemToObject(root, "frame", frame);

    cJSON* out_obj = cJSON_CreateObject();
    if (api_ptr && api_ptr->signage()) {
        const auto& ctrl = api_ptr->signage()->controller();
        cJSON_AddStringToObject(out_obj, "faultDomain", output_fault_domain_name(ctrl.last_fault_domain()));
        cJSON_AddBoolToObject(out_obj, "topologyValid", ctrl.is_topology_valid());
        cJSON_AddNumberToObject(out_obj, "topologyGeneration", ctrl.topology_generation());
        cJSON_AddNumberToObject(out_obj, "reconfigureAttempts", ctrl.topology_reconfigure_attempts());
        cJSON_AddNumberToObject(out_obj, "reconfigureFailures", ctrl.topology_reconfigure_failures());
        cJSON_AddNumberToObject(out_obj, "rollbacks", ctrl.topology_rollbacks());
        cJSON_AddNumberToObject(out_obj, "rollbackFailures", ctrl.topology_rollback_failures());
        cJSON_AddNumberToObject(out_obj, "lockContentions", ctrl.output_lock_contentions());
        cJSON_AddNumberToObject(out_obj, "maxLockWaitUs", ctrl.output_lock_max_wait_us());
        cJSON_AddNumberToObject(out_obj, "lifecycleMaxUs", ctrl.lifecycle_max_us());
        cJSON_AddNumberToObject(out_obj, "reconfigureMaxUs", ctrl.reconfigure_max_us());
    }
    cJSON_AddItemToObject(root, "output", out_obj);

    if (api_ptr && api_ptr->wifi()) {
        const WifiDiagnostics wdiag = api_ptr->wifi()->diagnostics();
        cJSON* wifi_obj = cJSON_CreateObject();
        const char* actual_mode_str = "null";
        switch (wdiag.actual_mode) {
            case WIFI_MODE_STA: actual_mode_str = "sta"; break;
            case WIFI_MODE_AP: actual_mode_str = "ap"; break;
            case WIFI_MODE_APSTA: actual_mode_str = "apsta"; break;
            default: actual_mode_str = "null"; break;
        }
        cJSON_AddStringToObject(wifi_obj, "actualMode", actual_mode_str);
        cJSON_AddNumberToObject(wifi_obj, "startAttempts", wdiag.start_attempts);
        cJSON_AddNumberToObject(wifi_obj, "startFailures", wdiag.start_failures);
        cJSON_AddNumberToObject(wifi_obj, "reconnectAttempts", wdiag.reconnect_attempts);
        cJSON_AddNumberToObject(wifi_obj, "reconnectFailures", wdiag.reconnect_failures);
        cJSON_AddNumberToObject(wifi_obj, "fallbackActivations", wdiag.fallback_activations);
        cJSON_AddNumberToObject(wifi_obj, "fallbackExitAttempts", wdiag.fallback_exit_attempts);
        cJSON_AddNumberToObject(wifi_obj, "fallbackExitFailures", wdiag.fallback_exit_failures);
        cJSON_AddNumberToObject(wifi_obj, "disconnectEvents", wdiag.disconnect_events);
        cJSON_AddNumberToObject(wifi_obj, "lastDisconnectReason", wdiag.last_disconnect_reason);
        cJSON_AddNumberToObject(wifi_obj, "lastError", wdiag.last_error);
        cJSON_AddItemToObject(root, "wifi", wifi_obj);
    }

    // Reset Diagnostics (LG-HW-004)
    const auto reset_d = lg::board::get_reset_diagnostics();
    cJSON* rst_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(rst_obj, "reason", static_cast<int>(reset_d.current_reason));
    cJSON_AddStringToObject(rst_obj, "reasonStr", reset_d.reason_str);
    cJSON_AddBoolToObject(rst_obj, "abnormal", reset_d.last_reset_abnormal);
    cJSON_AddNumberToObject(rst_obj, "consecutiveAbnormalBoots", reset_d.consecutive_abnormal_boots);
    cJSON_AddNumberToObject(rst_obj, "totalBoots", reset_d.total_boots);
    cJSON_AddBoolToObject(rst_obj, "previousBootReady", reset_d.previous_boot_reached_ready);
    cJSON_AddBoolToObject(rst_obj, "currentBootConfirmed", reset_d.current_boot_confirmed);
    cJSON_AddBoolToObject(rst_obj, "safeModeMandated", lg::board::is_safe_mode_mandated());
    cJSON_AddBoolToObject(rst_obj, "quarantineActive", lg::board::is_abnormal_reset_quarantine_active(static_cast<uint32_t>(esp_timer_get_time() / 1000ULL)));
    cJSON_AddNumberToObject(rst_obj, "bootSessionId", reset_d.boot_session_id);
    cJSON_AddBoolToObject(rst_obj, "brownoutDetectorEnabled",
#if CONFIG_ESP_BROWNOUT_DET
        true
#else
        false
#endif
    );
    cJSON_AddItemToObject(root, "reset", rst_obj);

    // Boot Output Diagnostics (LG-HW-004)
    const auto boot_out = lg::board::get_boot_output_diagnostics();
    cJSON* boot_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(boot_obj, "earlySafetyApplied", boot_out.early_safety_applied);
    cJSON_AddBoolToObject(boot_obj, "pcaOeHighAtBootstrap", boot_out.pca_oe_high_at_bootstrap);
    cJSON_AddNumberToObject(boot_obj, "aledLowMask", boot_out.aled_low_mask);
    cJSON_AddBoolToObject(boot_obj, "firstAddressableBlackConfirmed", boot_out.first_addressable_black_confirmed);
    cJSON_AddBoolToObject(boot_obj, "firstPwmZeroConfirmed", boot_out.first_pwm_zero_confirmed);
    cJSON_AddNumberToObject(boot_obj, "firstOutputCommitMs", boot_out.first_output_commit_ms);
    cJSON_AddItemToObject(root, "bootOutput", boot_obj);

    // Power Transient Diagnostics & Activation Timing (LG-HW-004)
    if (api_ptr && api_ptr->signage()) {
        const auto pwr_trans = api_ptr->signage()->power_diagnostics();
        cJSON* pwr_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(pwr_obj, "requestedPowerMw", pwr_trans.requested_power_mw);
        cJSON_AddNumberToObject(pwr_obj, "limitedPowerMw", pwr_trans.limited_power_mw);
        cJSON_AddNumberToObject(pwr_obj, "previousLimitedPowerMw", pwr_trans.previous_limited_power_mw);
        cJSON_AddNumberToObject(pwr_obj, "maxObservedStepMw", pwr_trans.max_observed_step_mw);
        cJSON_AddNumberToObject(pwr_obj, "powerStepClamps", pwr_trans.power_step_clamps);
        cJSON_AddNumberToObject(pwr_obj, "enableEvents", pwr_trans.enable_events);
        cJSON_AddNumberToObject(pwr_obj, "rampEvents", pwr_trans.ramp_events);
        cJSON_AddNumberToObject(pwr_obj, "rampMs", pwr_trans.ramp_ms);
        cJSON_AddItemToObject(root, "powerTransient", pwr_obj);

        // HW-005.14: Truthful supply and power diagnostics
        cJSON* pwr_full = cJSON_CreateObject();
        const char* model_state_str = (pwr_trans.model_state == PowerEstimateState::Complete) ? "complete" :
                                      (pwr_trans.model_state == PowerEstimateState::Partial) ? "partial" : "unknown";
        cJSON_AddStringToObject(pwr_full, "modelState", model_state_str);
        cJSON_AddNumberToObject(pwr_full, "requestedMw", pwr_trans.requested_power_mw);
        cJSON_AddNumberToObject(pwr_full, "limitedMw", pwr_trans.limited_power_mw);
        cJSON_AddNullToObject(pwr_full, "measuredMw"); // Truthful: no physical current/power sensor
        cJSON_AddBoolToObject(pwr_full, "limitActive", pwr_trans.requested_power_mw > pwr_trans.limited_power_mw);

        cJSON* domains_arr = cJSON_CreateArray();
        for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
            const auto& dom = pwr_trans.supply_domains[s];
            if (dom.enabled || dom.usable_power_mw > 0) {
                cJSON* dom_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(dom_obj, "id", dom.id);
                cJSON_AddNumberToObject(dom_obj, "voltageMv", dom.voltage_mv);
                cJSON_AddNumberToObject(dom_obj, "ratedCurrentMa", dom.rated_current_ma);
                cJSON_AddNumberToObject(dom_obj, "usablePowerMw", dom.usable_power_mw);
                cJSON_AddNumberToObject(dom_obj, "estimatedLoadMw", dom.estimated_load_mw);
                cJSON_AddNumberToObject(dom_obj, "channelMask", dom.channel_mask);
                cJSON_AddNumberToObject(dom_obj, "throttleEvents", dom.throttle_events);
                cJSON_AddItemToArray(domains_arr, dom_obj);
            }
        }
        cJSON_AddItemToObject(pwr_full, "supplyDomains", domains_arr);
        cJSON_AddNumberToObject(pwr_full, "unmodeledChannelMask", pwr_trans.unmodeled_channel_mask);
        cJSON_AddNumberToObject(pwr_full, "channelThrottleEvents", pwr_trans.channel_throttle_events);
        cJSON_AddNumberToObject(pwr_full, "unknownLoadWarnings", pwr_trans.unknown_load_warnings);
        cJSON_AddNumberToObject(pwr_full, "rampEvents", pwr_trans.ramp_events);
        cJSON_AddNumberToObject(pwr_full, "maxObservedStepMw", pwr_trans.max_observed_step_mw);
        cJSON_AddNumberToObject(pwr_full, "overflowRejectionAttempts", pwr_trans.overflow_rejection_attempts);
        cJSON_AddItemToObject(root, "power", pwr_full);

        const auto act_timing = api_ptr->signage()->activation_timing();
        cJSON* act_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(act_obj, "armMs", act_timing.arm_ms);
        cJSON_AddNumberToObject(act_obj, "firstZeroBlackMs", act_timing.first_zero_black_ms);
        cJSON_AddNumberToObject(act_obj, "firstNonzeroMs", act_timing.first_nonzero_ms);
        cJSON_AddNumberToObject(act_obj, "rampCompleteMs", act_timing.ramp_complete_ms);
        cJSON_AddNumberToObject(act_obj, "lastArmToZeroMs", act_timing.last_arm_to_zero_ms);
        cJSON_AddNumberToObject(act_obj, "lastZeroToNonzeroMs", act_timing.last_zero_to_nonzero_ms);
        cJSON_AddNumberToObject(act_obj, "lastRampDurationMs", act_timing.last_ramp_duration_ms);
        cJSON_AddItemToObject(root, "activationTiming", act_obj);
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

// ── LG-HW-004: Boot-safe bench verification endpoint ─────────────────────
esp_err_t get_boot_safe_verify(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    auto* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }

    const auto reset_d = lg::board::get_reset_diagnostics();
    const auto boot_out = lg::board::get_boot_output_diagnostics();
    const int oe_level = lg::board::sample_pca_oe_level();
    const uint8_t aled_mask = lg::board::sample_addressable_pin_mask();
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    bool outputs_armed = false;
    bool quarantine_active = lg::board::is_abnormal_reset_quarantine_active(now_ms);
    if (api_ptr && api_ptr->signage()) {
        outputs_armed = (api_ptr->signage()->output_state() == LightingRuntime::OutputState::Armed ||
                         api_ptr->signage()->output_state() == LightingRuntime::OutputState::Healthy);
    }

    cJSON_AddNumberToObject(root, "oeLevel", oe_level);
    cJSON_AddBoolToObject(root, "oeHigh", oe_level == 1);
    cJSON_AddNumberToObject(root, "addressableMask", aled_mask);
    cJSON_AddBoolToObject(root, "addressableAllLow", aled_mask == 0);
    cJSON_AddStringToObject(root, "resetReason", reset_d.reason_str);
    cJSON_AddBoolToObject(root, "abnormalReset", reset_d.last_reset_abnormal);
    cJSON_AddBoolToObject(root, "brownoutDetectorEnabled",
#if CONFIG_ESP_BROWNOUT_DET
        true
#else
        false
#endif
    );
    cJSON_AddBoolToObject(root, "outputsArmed", outputs_armed);
    cJSON_AddBoolToObject(root, "quarantineActive", quarantine_active);
    cJSON_AddBoolToObject(root, "firstBlackConfirmed", boot_out.first_addressable_black_confirmed);
    cJSON_AddBoolToObject(root, "firstZeroConfirmed", boot_out.first_pwm_zero_confirmed);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }
    esp_err_t ret = send_json(request, json);
    cJSON_free(json);
    return ret;
}

// ── LG-038.3 §4: detailed memory diagnostics (Installer+). ───────────────
// Real 8-bit heap truth, fragmentation ratio (§46) and per-task stack
// high-water data (§29). Task data stays here, NOT in the 5 Hz snapshot.
esp_err_t get_diagnostics_memory(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    auto* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    const RuntimeMemorySnapshot mem = capture_runtime_memory();
    const RuntimeMemoryCounters& counters = runtime_memory_counters();

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        note_api_allocation_failure();
        return send_error(request, "500 Internal Server Error", "json_encode_failed");
    }

    cJSON_AddNumberToObject(root, "free8", mem.free_8bit);
    cJSON_AddNumberToObject(root, "largest8", mem.largest_8bit);
    cJSON_AddNumberToObject(root, "minimumFree", mem.minimum_free_heap);
    cJSON_AddNumberToObject(root, "freeInternal", mem.free_internal);
    cJSON_AddNumberToObject(root, "largestInternal", mem.largest_internal);
    cJSON_AddNumberToObject(root, "freeDma", mem.free_dma);
    cJSON_AddNumberToObject(root, "largestDma", mem.largest_dma);
    // §46: diagnostics-only fragmentation metric; no universal PASS threshold.
    cJSON_AddNumberToObject(root, "fragmentationRatio", fragmentation_ratio(mem));

    const MemoryHealth health = evaluate_memory_health(mem);
    cJSON_AddStringToObject(root, "health", memory_health_name(health));

    cJSON* failures = cJSON_AddObjectToObject(root, "allocationFailures");
    cJSON_AddNumberToObject(failures, "apiAllocationFailures", counters.api_allocation_failures);
    cJSON_AddNumberToObject(failures, "previewAllocationFailures", counters.preview_allocation_failures);
    cJSON_AddNumberToObject(failures, "rmtAllocationFailures", counters.rmt_allocation_failures);
    cJSON_AddNumberToObject(failures, "factoryTaskStartFailures", counters.factory_task_start_failures);
    cJSON_AddNumberToObject(failures, "networkTaskStartFailures", counters.network_task_start_failures);

    if (api_ptr) {
        const ServiceStateTruth services = api_ptr->service_state_truth();
        cJSON* svc = cJSON_AddObjectToObject(root, "services");
        cJSON_AddBoolToObject(svc, "lighting", services.lighting);
        cJSON_AddBoolToObject(svc, "factoryWorker", services.factory_worker);
        cJSON_AddBoolToObject(svc, "scheduler", services.scheduler);
        cJSON_AddBoolToObject(svc, "api", services.api);
        cJSON_AddBoolToObject(svc, "mdns", services.mdns);
        cJSON_AddBoolToObject(svc, "fleetDiscovery", services.fleet_discovery);
        cJSON_AddBoolToObject(svc, "artnet", services.artnet);
        cJSON_AddBoolToObject(svc, "sacn", services.sacn);
        cJSON_AddBoolToObject(svc, "nodeSync", services.node_sync);
        cJSON_AddBoolToObject(svc, "mqtt", services.mqtt);
        cJSON_AddBoolToObject(svc, "networkApplyWorker", services.network_apply_worker);
    }

    // §29: per-task configured stack + minimum observed free (bytes).
    cJSON* tasks = cJSON_AddArrayToObject(root, "tasks");
    if (tasks) {
        TaskStackInfo stack_info[kMaxTrackedTasks];
        task_stack_registry().sample_all();
        const size_t n = task_stack_registry().copy(stack_info, kMaxTrackedTasks);
        for (size_t i = 0; i < n; ++i) {
            cJSON* t = cJSON_CreateObject();
            if (!t) break;
            cJSON_AddStringToObject(t, "name", stack_info[i].name ? stack_info[i].name : "");
            cJSON_AddNumberToObject(t, "stackBytes", stack_info[i].configured_bytes);
            cJSON_AddNumberToObject(t, "minimumFreeStack", stack_info[i].minimum_free_bytes);
            cJSON_AddItemToArray(tasks, t);
        }
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
    if (!logs) {
        note_api_allocation_failure();
        return send_error(request, "500 Internal Server Error", "Out of Memory");
    }
    
    DiagnosticLog::instance().copy_logs(logs, DiagnosticLog::kBufferSize + 1);
    
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_set_type(request, "text/plain");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id");
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

    char session_hex[65] = {};
    uint32_t expires_in = 0;
    esp_err_t err = auth_mgr->bootstrap(tok->valuestring, session_hex, &expires_in);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "bootstrap_failed");
    }

    api::AuditLog::instance().log(api::ApiRole::Admin, "auth_bootstrap", "Admin credential bootstrapped");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddBoolToObject(resp, "provisioned", true);
    cJSON_AddBoolToObject(resp, "authenticated", true);
    cJSON_AddStringToObject(resp, "role", "admin");
    cJSON_AddStringToObject(resp, "sessionToken", session_hex);
    cJSON_AddNumberToObject(resp, "expiresIn", expires_in);
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
    cJSON_AddBoolToObject(resp, "setupRequired", !auth_mgr->is_provisioned());
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
    cJSON_AddNumberToObject(obj, "checksum", scene.checksum);

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
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id");
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

struct TimelineReference {
    uint8_t channel = 0;
    uint8_t step = 0;
};

struct SceneReferences {
    uint8_t scene_index = 0;
    bool active = false;
    bool fallback = false;
    bool active_cue_transport = false;   // LG-034 §43: scene used by the RUNNING cue snapshot
    uint8_t active_cue_list = 0;
    uint8_t active_cue = 0;
    std::vector<uint8_t> cues;
    std::vector<uint8_t> timeline_tracks;
    std::vector<TimelineReference> timeline_steps;
    std::vector<uint8_t> schedules;

    bool in_use() const {
        return active || fallback || active_cue_transport || !cues.empty() ||
               !timeline_tracks.empty() || !timeline_steps.empty() || !schedules.empty();
    }
};

static SceneReferences evaluate_scene_references(FactoryApi* api_ptr, uint8_t scene_index) {
    SceneReferences refs;
    refs.scene_index = scene_index;
    if (!api_ptr || !api_ptr->config()) return refs;

    // 1. Active scene
    if (api_ptr->signage() && api_ptr->signage()->scene_manager().active_scene_index() == scene_index) {
        refs.active = true;
    }

    // 2. Fallback preset in device config
    DeviceConfig dcfg;
    if (api_ptr->config()->load_config(dcfg) == ESP_OK) {
        if (dcfg.fallback_preset_index == scene_index) {
            refs.fallback = true;
        }
    }

    // 3. Cue Lists (persisted)
    for (uint8_t i = 0; i < kMaxCueLists; ++i) {
        CueList cl;
        if (api_ptr->config()->load_cue_list(i, cl) == ESP_OK && cl.count > 0) {
            for (uint8_t c = 0; c < cl.count && c < kMaxCuesPerList; ++c) {
                if (cl.cues[c].scene_index == scene_index) {
                    refs.cues.push_back(i);
                    break;
                }
            }
        }
    }

    // 3b. LG-034 §43: the RUNNING Cue snapshot protects its scenes even after
    // the saved list was edited to no longer reference them.
    if (api_ptr->cue_runner()) {
        const auto cue_snap = api_ptr->cue_runner()->snapshot(
            api_ptr->show_clock_now_ms());
        if (cue_snap.running) {
            if (cue_snap.current_scene == scene_index) {
                refs.active_cue_transport = true;
                refs.active_cue_list = cue_snap.list_index;
                refs.active_cue = cue_snap.current_cue;
            }
            // The prepared next cue in Manual mode must also stay available.
            if (!refs.active_cue_transport && cue_snap.mode == CueRunMode::Manual &&
                cue_snap.next_scene == scene_index) {
                refs.active_cue_transport = true;
                refs.active_cue_list = cue_snap.list_index;
                refs.active_cue = cue_snap.next_cue;
            }
        }
    }

    // 4. Timeline sequence tracks
    Sequence seq;
    if (api_ptr->config()->load_sequence(seq) == ESP_OK) {
        for (uint8_t t = 0; t < 16; ++t) {
            if (seq.tracks[t].enabled) {
                for (uint8_t s = 0; s < seq.tracks[t].num_steps && s < 16; ++s) {
                    if (seq.tracks[t].steps[s].preset_index == scene_index) {
                        refs.timeline_tracks.push_back(t);
                        refs.timeline_steps.push_back({t, s});
                    }
                }
            }
        }
    }

    // 5. Calendar schedule
    CalendarSchedule sched;
    if (api_ptr->config()->load_calendar_schedule(sched) == ESP_OK) {
        for (uint8_t i = 0; i < 8; ++i) {
            if (sched.entries[i].enabled && sched.entries[i].action == CalendarAction::Preset && sched.entries[i].preset_index == scene_index) {
                refs.schedules.push_back(i);
            }
        }
    }

    return refs;
}

esp_err_t get_preset_references(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    uint8_t target_index = 0;
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
                target_index = static_cast<uint8_t>(idx);
            }
        }
    } else {
        return send_error(request, "400 Bad Request", "missing_index");
    }

    SceneReferences refs = evaluate_scene_references(api_ptr, target_index);

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "sceneIndex", refs.scene_index);
    cJSON_AddBoolToObject(obj, "active", refs.active);
    cJSON_AddBoolToObject(obj, "fallback", refs.fallback);

    cJSON* cues_arr = cJSON_CreateArray();
    for (uint8_t c : refs.cues) cJSON_AddItemToArray(cues_arr, cJSON_CreateNumber(c));
    cJSON_AddItemToObject(obj, "cues", cues_arr);

    cJSON* tt_arr = cJSON_CreateArray();
    for (uint8_t t : refs.timeline_tracks) cJSON_AddItemToArray(tt_arr, cJSON_CreateNumber(t));
    cJSON_AddItemToObject(obj, "timelineTracks", tt_arr);

    cJSON* s_arr = cJSON_CreateArray();
    for (uint8_t s : refs.schedules) cJSON_AddItemToArray(s_arr, cJSON_CreateNumber(s));
    cJSON_AddItemToObject(obj, "schedules", s_arr);

    char* json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return send_error(request, "500 Internal Server Error", "out_of_memory");
    esp_err_t res = send_json(request, json);
    cJSON_free(json);
    return res;
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

    uint8_t u_idx = static_cast<uint8_t>(index);
    SceneReferences refs = evaluate_scene_references(api_ptr, u_idx);
    if (refs.in_use()) {
        cJSON* err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "error", "scene_in_use");
        cJSON* ref_arr = cJSON_CreateArray();
        if (refs.active) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "active");
            cJSON_AddNumberToObject(item, "index", u_idx);
            cJSON_AddItemToArray(ref_arr, item);
        }
        if (refs.active_cue_transport) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "activeCueTransport");
            cJSON_AddNumberToObject(item, "cueList", refs.active_cue_list);
            cJSON_AddNumberToObject(item, "cue", refs.active_cue);
            cJSON_AddItemToArray(ref_arr, item);
        }
        if (refs.fallback) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "fallback");
            cJSON_AddNumberToObject(item, "index", u_idx);
            cJSON_AddItemToArray(ref_arr, item);
        }
        for (uint8_t c : refs.cues) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "cueList");
            cJSON_AddNumberToObject(item, "index", c);
            cJSON_AddItemToArray(ref_arr, item);
        }
        for (const auto& ts : refs.timeline_steps) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "timeline");
            cJSON_AddNumberToObject(item, "channel", ts.channel);
            cJSON_AddNumberToObject(item, "step", ts.step);
            cJSON_AddItemToArray(ref_arr, item);
        }
        for (uint8_t s : refs.schedules) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "schedule");
            cJSON_AddNumberToObject(item, "index", s);
            cJSON_AddItemToArray(ref_arr, item);
        }
        cJSON_AddItemToObject(err_obj, "references", ref_arr);
        char* json = cJSON_PrintUnformatted(err_obj);
        cJSON_Delete(err_obj);
        if (!json) return send_error(request, "500 Internal Server Error", "out_of_memory");
        httpd_resp_set_status(request, "409 Conflict");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_send(request, json, strlen(json));
        cJSON_free(json);
        return ESP_OK;
    }

    api_ptr->config()->delete_scene(u_idx);
    api_ptr->config()->delete_scene_motion(u_idx);

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
    cJSON_AddNumberToObject(root, "syncProtocol", CapabilitiesManifest::kProtocolSync);
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

static bool parse_and_validate_layer_obj(const cJSON* l_item, LayerConfig& layer, const char*& err_code) {
    if (!l_item || !cJSON_IsObject(l_item)) {
        err_code = "invalid_layer_object";
        return false;
    }

    const cJSON* effect_j = cJSON_GetObjectItemCaseSensitive(l_item, "effect");
    if (effect_j) {
        if (cJSON_IsString(effect_j)) {
            SignageEffect eff = SignageEffect::Solid;
            if (!parse_signage_effect(effect_j->valuestring, eff)) {
                err_code = "invalid_layer_effect";
                return false;
            }
            layer.effect = static_cast<uint8_t>(eff);
        } else if (cJSON_IsNumber(effect_j)) {
            if (effect_j->valuedouble != effect_j->valueint ||
                effect_j->valueint < 0 || effect_j->valueint >= static_cast<int>(kSignageEffectCount)) {
                err_code = "invalid_layer_effect";
                return false;
            }
            layer.effect = static_cast<uint8_t>(effect_j->valueint);
        } else {
            err_code = "invalid_layer_effect";
            return false;
        }
    }

    const cJSON* blend_j = cJSON_GetObjectItemCaseSensitive(l_item, "blendMode");
    if (blend_j) {
        if (cJSON_IsString(blend_j)) {
            if (!parse_blend_mode(blend_j->valuestring, layer.blend_mode)) {
                err_code = "invalid_blend_mode";
                return false;
            }
        } else if (cJSON_IsNumber(blend_j)) {
            if (blend_j->valueint < 0 || blend_j->valueint > 3) {
                err_code = "invalid_blend_mode";
                return false;
            }
            layer.blend_mode = static_cast<BlendMode>(blend_j->valueint);
        } else {
            err_code = "invalid_blend_mode";
            return false;
        }
    }

    const cJSON* pc = cJSON_GetObjectItemCaseSensitive(l_item, "primaryColor");
    if (pc) {
        if (!cJSON_IsObject(pc)) { err_code = "invalid_layer_color"; return false; }
        const cJSON* r = cJSON_GetObjectItemCaseSensitive(pc, "r");
        const cJSON* g = cJSON_GetObjectItemCaseSensitive(pc, "g");
        const cJSON* b = cJSON_GetObjectItemCaseSensitive(pc, "b");
        if (!r || !cJSON_IsNumber(r) || r->valuedouble < 0 || r->valuedouble > 255 ||
            !g || !cJSON_IsNumber(g) || g->valuedouble < 0 || g->valuedouble > 255 ||
            !b || !cJSON_IsNumber(b) || b->valuedouble < 0 || b->valuedouble > 255) {
            err_code = "invalid_layer_color";
            return false;
        }
        layer.primary_color.r = static_cast<uint8_t>(r->valueint);
        layer.primary_color.g = static_cast<uint8_t>(g->valueint);
        layer.primary_color.b = static_cast<uint8_t>(b->valueint);
    }

    const cJSON* sc = cJSON_GetObjectItemCaseSensitive(l_item, "secondaryColor");
    if (sc) {
        if (!cJSON_IsObject(sc)) { err_code = "invalid_layer_color"; return false; }
        const cJSON* r = cJSON_GetObjectItemCaseSensitive(sc, "r");
        const cJSON* g = cJSON_GetObjectItemCaseSensitive(sc, "g");
        const cJSON* b = cJSON_GetObjectItemCaseSensitive(sc, "b");
        if (!r || !cJSON_IsNumber(r) || r->valuedouble < 0 || r->valuedouble > 255 ||
            !g || !cJSON_IsNumber(g) || g->valuedouble < 0 || g->valuedouble > 255 ||
            !b || !cJSON_IsNumber(b) || b->valuedouble < 0 || b->valuedouble > 255) {
            err_code = "invalid_layer_color";
            return false;
        }
        layer.secondary_color.r = static_cast<uint8_t>(r->valueint);
        layer.secondary_color.g = static_cast<uint8_t>(g->valueint);
        layer.secondary_color.b = static_cast<uint8_t>(b->valueint);
    }

    const cJSON* en = cJSON_GetObjectItemCaseSensitive(l_item, "enabled");
    if (en) {
        if (!cJSON_IsBool(en)) {
            err_code = "invalid_layer_enabled";
            return false;
        }
        layer.enabled = cJSON_IsTrue(en);
    }

    const cJSON* spd = cJSON_GetObjectItemCaseSensitive(l_item, "speedMs");
    if (spd) {
        // SceneConfig::reset() stores zero speed in unused disabled layers.
        // Preserve that representation across backup/restore; an enabled
        // effect must still meet the ordinary timing bounds.
        const bool unused_zero = !layer.enabled && spd->valuedouble == 0;
        if (!lg::json::is_integer(spd) || (!unused_zero && spd->valuedouble < 10) || spd->valuedouble > 60000) {
            err_code = "invalid_layer_speed";
            return false;
        }
        layer.speed_ms = static_cast<uint16_t>(spd->valueint);
    }

    const cJSON* fx = cJSON_GetObjectItemCaseSensitive(l_item, "fxParam");
    if (fx) {
        if (!cJSON_IsNumber(fx) || fx->valuedouble < 0 || fx->valuedouble > 255) {
            err_code = "invalid_layer_fx_param";
            return false;
        }
        layer.fx_param = static_cast<uint8_t>(fx->valueint);
    }

    const cJSON* pal = cJSON_GetObjectItemCaseSensitive(l_item, "paletteId");
    if (pal) {
        if (!cJSON_IsNumber(pal) || pal->valuedouble != pal->valueint ||
            pal->valueint < 0 || pal->valueint >= static_cast<int>(kSignagePaletteCount)) {
            err_code = "invalid_layer_palette";
            return false;
        }
        layer.palette_id = static_cast<uint8_t>(pal->valueint);
    }

    const cJSON* op = cJSON_GetObjectItemCaseSensitive(l_item, "opacity");
    if (op) {
        if (!cJSON_IsNumber(op) || op->valuedouble < 0 || op->valuedouble > 255) {
            err_code = "invalid_layer_opacity";
            return false;
        }
        layer.opacity = static_cast<uint8_t>(op->valueint);
    }


    return true;
}

static bool parse_and_validate_motion_json(const cJSON* motion_j, uint8_t index, SceneMotionConfig& motion, const char*& err_code) {
    if (!motion_j || !cJSON_IsObject(motion_j)) {
        err_code = "invalid_motion_object";
        return false;
    }
    motion.schema_version = 1;

    const cJSON* cur_j = cJSON_GetObjectItemCaseSensitive(motion_j, "transitionCurve");
    if (cur_j && cJSON_IsString(cur_j)) {
        if (!parse_transition_curve(cur_j->valuestring, motion.transition_curve)) {
            err_code = "invalid_transition_curve";
            return false;
        }
    } else {
        motion.transition_curve = TransitionCurve::EaseInOut;
    }

    const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(motion_j, "tracks");
    if (tracks) {
        if (!cJSON_IsArray(tracks)) {
            err_code = "invalid_motion_tracks";
            return false;
        }
        int t_len = cJSON_GetArraySize(tracks);
        if (t_len > kAutomationTracksPerScene) {
            err_code = "too_many_motion_tracks";
            return false;
        }
        uint8_t t_count = 0;
        const cJSON* t_item = nullptr;
        cJSON_ArrayForEach(t_item, tracks) {
            auto& tr = motion.tracks[t_count];

            const cJSON* tgt_j = cJSON_GetObjectItemCaseSensitive(t_item, "target");
            if (!tgt_j || !cJSON_IsString(tgt_j) || !parse_automation_target(tgt_j->valuestring, tr.target)) {
                err_code = "invalid_motion_target";
                return false;
            }

            const cJSON* ch_j = cJSON_GetObjectItemCaseSensitive(t_item, "channel");
            const cJSON* z_j = cJSON_GetObjectItemCaseSensitive(t_item, "zone");
            if (ch_j && cJSON_IsNumber(ch_j)) {
                if (ch_j->valueint < 0 || ch_j->valueint > 15) {
                    err_code = "invalid_motion_channel";
                    return false;
                }
                tr.channel = static_cast<uint8_t>(ch_j->valueint);
            } else if (z_j && cJSON_IsNumber(z_j)) {
                if (z_j->valueint < 0 || z_j->valueint > 15) {
                    err_code = "invalid_motion_channel";
                    return false;
                }
                tr.channel = static_cast<uint8_t>(z_j->valueint);
            } else {
                tr.channel = 0;
            }

            const cJSON* l_j = cJSON_GetObjectItemCaseSensitive(t_item, "layer");
            if (l_j) {
                if (!cJSON_IsNumber(l_j) || l_j->valueint < 0 || l_j->valueint > 2) {
                    err_code = "invalid_motion_layer";
                    return false;
                }
                tr.layer = static_cast<uint8_t>(l_j->valueint);
            } else {
                tr.layer = 0;
            }

            const cJSON* c_j = cJSON_GetObjectItemCaseSensitive(t_item, "curve");
            if (c_j && cJSON_IsString(c_j)) {
                if (!parse_automation_curve(c_j->valuestring, tr.curve)) {
                    err_code = "invalid_motion_curve";
                    return false;
                }
            } else {
                tr.curve = AutomationCurve::EaseInOut;
            }

            const cJSON* loop_j = cJSON_GetObjectItemCaseSensitive(t_item, "loop");
            if (loop_j) {
                if (!cJSON_IsBool(loop_j)) {
                    err_code = "invalid_motion_loop";
                    return false;
                }
                tr.loop = cJSON_IsTrue(loop_j);
            } else {
                tr.loop = true;
            }

            const cJSON* per_j = cJSON_GetObjectItemCaseSensitive(t_item, "periodMs");
            if (per_j) {
                if (!cJSON_IsNumber(per_j) || per_j->valuedouble < 100 || per_j->valuedouble > 3600000) {
                    err_code = "invalid_motion_period";
                    return false;
                }
                tr.period_ms = static_cast<uint32_t>(per_j->valuedouble);
            } else {
                tr.period_ms = 5000;
            }

            const cJSON* pts = cJSON_GetObjectItemCaseSensitive(t_item, "points");
            if (pts) {
                if (!cJSON_IsArray(pts)) {
                    err_code = "invalid_motion_points";
                    return false;
                }
                int p_len = cJSON_GetArraySize(pts);
                if (p_len > kAutomationPointsPerTrack) {
                    err_code = "too_many_motion_points";
                    return false;
                }
                uint8_t p_count = 0;
                const cJSON* p_item = nullptr;
                int prev_pos = -1;
                cJSON_ArrayForEach(p_item, pts) {
                    const cJSON* pos_j = cJSON_GetObjectItemCaseSensitive(p_item, "position");
                    const cJSON* val_j = cJSON_GetObjectItemCaseSensitive(p_item, "value");
                    if (!pos_j || !cJSON_IsNumber(pos_j) || pos_j->valuedouble < 0 || pos_j->valuedouble > 65535) {
                        err_code = "invalid_motion_point_position";
                        return false;
                    }
                    if (!val_j || !cJSON_IsNumber(val_j) || val_j->valuedouble < 0 || val_j->valuedouble > 65535) {
                        err_code = "invalid_motion_point_value";
                        return false;
                    }

                    int pos_int = static_cast<int>(pos_j->valuedouble);
                    int val_int = static_cast<int>(val_j->valuedouble);

                    if (tr.target == AutomationTarget::GlobalBrightness && val_int > 255) {
                        err_code = "invalid_motion_point_value";
                        return false;
                    }
                    if (tr.target == AutomationTarget::LayerOpacity && val_int > 255) {
                        err_code = "invalid_motion_point_value";
                        return false;
                    }
                    if (tr.target == AutomationTarget::LayerSpeed && (val_int < 10 || val_int > 60000)) {
                        err_code = "invalid_motion_point_value";
                        return false;
                    }
                    if (tr.target == AutomationTarget::LayerParameter && val_int > 255) {
                        err_code = "invalid_motion_point_value";
                        return false;
                    }

                    if (p_count > 0 && pos_int <= prev_pos) {
                        err_code = "invalid_motion_points_order";
                        return false;
                    }
                    prev_pos = pos_int;

                    tr.points[p_count].position = static_cast<uint16_t>(pos_int);
                    tr.points[p_count].value = static_cast<uint16_t>(val_int);
                    p_count++;
                }
                tr.point_count = p_count;
            }
            t_count++;
        }
        motion.track_count = t_count;
    }
    return true;
}

esp_err_t put_preset(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (request->content_len > kMaxPresetBodyBytes) {
        return send_error(request, "413 Payload Too Large", "scene_payload_too_large");
    }
    if (read_json(request, &root, kMaxPresetBodyBytes) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    uint8_t index = 0;
    if (!json_u8(root, "index", 0, kMaxPresets - 1, index)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "missing_index");
    }

    // Check revision conflicts if expectedChecksum is provided
    const cJSON* exp_sc = cJSON_GetObjectItemCaseSensitive(root, "expectedSceneChecksum");
    if (exp_sc && cJSON_IsNumber(exp_sc)) {
        SceneConfig cur_sc{};
        if (api_ptr->config()->load_scene(index, cur_sc) == ESP_OK) {
            if (cur_sc.checksum != static_cast<uint32_t>(exp_sc->valuedouble)) {
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "scene_conflict");
            }
        }
    }

    const cJSON* exp_mc = cJSON_GetObjectItemCaseSensitive(root, "expectedMotionChecksum");
    if (exp_mc && cJSON_IsNumber(exp_mc)) {
        SceneMotionConfig cur_mc{};
        if (api_ptr->config()->load_scene_motion(index, cur_mc) == ESP_OK) {
            if (cur_mc.checksum != static_cast<uint32_t>(exp_mc->valuedouble)) {
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "scene_conflict");
            }
        }
    }

    SceneConfig scene{};
    const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (name_j) {
        if (!cJSON_IsString(name_j)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_name");
        }
        if (strlen(name_j->valuestring) > 23) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_scene_name_length");
        }
        strncpy(scene.name, name_j->valuestring, sizeof(scene.name) - 1);
        scene.name[sizeof(scene.name) - 1] = '\0';
    }

    const cJSON* gb_j = cJSON_GetObjectItemCaseSensitive(root, "globalBrightness");
    if (gb_j) {
        if (!cJSON_IsNumber(gb_j) || gb_j->valuedouble < 0 || gb_j->valuedouble > 255) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_globalBrightness");
        }
        scene.global_brightness = static_cast<uint8_t>(gb_j->valueint);
    }

    const cJSON* tr_j = cJSON_GetObjectItemCaseSensitive(root, "transitionMs");
    if (tr_j) {
        if (!cJSON_IsNumber(tr_j) || tr_j->valuedouble < 0 || tr_j->valuedouble > 10000) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_field_transitionMs");
        }
        scene.transition_ms = static_cast<uint16_t>(tr_j->valueint);
    }

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
                    const char* l_err = nullptr;
                    if (!parse_and_validate_layer_obj(l_item, scene.channels[c_idx].layers[l_idx], l_err)) {
                        cJSON_Delete(root);
                        return send_error(request, "400 Bad Request", l_err ? l_err : "invalid_layer");
                    }
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
                        const char* l_err = nullptr;
                        if (!parse_and_validate_layer_obj(l_item, scene.channels[z_idx].layers[l_idx], l_err)) {
                            cJSON_Delete(root);
                            return send_error(request, "400 Bad Request", l_err ? l_err : "invalid_layer");
                        }
                        l_idx++;
                    }
                }
                z_idx++;
            }
        }
    }

    // Optional motion inside scene payload
    SceneMotionConfig motion{};
    bool has_motion = false;
    const cJSON* motion_j = cJSON_GetObjectItemCaseSensitive(root, "motion");
    if (motion_j && !cJSON_IsNull(motion_j)) {
        const char* m_err = nullptr;
        if (!parse_and_validate_motion_json(motion_j, index, motion, m_err)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", m_err ? m_err : "invalid_motion");
        }
        has_motion = true;
    }

    cJSON_Delete(root);

    // Backup previous state for transactional rollback
    SceneConfig prev_scene{};
    bool prev_scene_exists = (api_ptr->config()->load_scene(index, prev_scene) == ESP_OK);
    SceneMotionConfig prev_motion{};
    bool prev_motion_exists = (api_ptr->config()->load_scene_motion(index, prev_motion) == ESP_OK);

    esp_err_t err = api_ptr->config()->save_scene(index, scene);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    if (has_motion) {
        err = api_ptr->config()->save_scene_motion(index, motion);
        if (err != ESP_OK) {
            // Rollback scene
            if (prev_scene_exists) api_ptr->config()->save_scene(index, prev_scene);
            else api_ptr->config()->delete_scene(index);
            return send_error(request, "500 Internal Server Error", "nvs_write_failed");
        }

        // Verify readback
        SceneConfig verify_sc{};
        SceneMotionConfig verify_mc{};
        if (api_ptr->config()->load_scene(index, verify_sc) != ESP_OK ||
            api_ptr->config()->load_scene_motion(index, verify_mc) != ESP_OK) {
            // Rollback both
            if (prev_scene_exists) api_ptr->config()->save_scene(index, prev_scene);
            else api_ptr->config()->delete_scene(index);
            if (prev_motion_exists) api_ptr->config()->save_scene_motion(index, prev_motion);
            else api_ptr->config()->delete_scene_motion(index);
            return send_error(request, "500 Internal Server Error", "nvs_readback_failed");
        }
        scene.checksum = verify_sc.checksum;
        motion.checksum = verify_mc.checksum;
    } else {
        SceneConfig verify_sc{};
        if (api_ptr->config()->load_scene(index, verify_sc) == ESP_OK) {
            scene.checksum = verify_sc.checksum;
        }
    }

    char detail[64];
    snprintf(detail, sizeof(detail), "Preset %d updated", index);
    api::AuditLog::instance().log(api::ApiRole::Operator, "preset_update", detail);

    char resp[128];
    if (has_motion) {
        snprintf(resp, sizeof(resp), "{\"saved\":true,\"index\":%u,\"sceneChecksum\":%" PRIu32 ",\"motionChecksum\":%" PRIu32 "}",
                 index, scene.checksum, motion.checksum);
    } else {
        snprintf(resp, sizeof(resp), "{\"saved\":true,\"index\":%u,\"sceneChecksum\":%" PRIu32 "}",
                 index, scene.checksum);
    }
    return send_json(request, resp, "200 OK");
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
        cJSON_AddNumberToObject(obj, "checksum", sm.checksum);
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

    const cJSON* exp_mc = cJSON_GetObjectItemCaseSensitive(root, "expectedChecksum");
    if (!exp_mc) exp_mc = cJSON_GetObjectItemCaseSensitive(root, "expectedMotionChecksum");
    if (exp_mc && cJSON_IsNumber(exp_mc)) {
        SceneMotionConfig cur_mc{};
        if (api_ptr->config()->load_scene_motion(index, cur_mc) == ESP_OK) {
            if (cur_mc.checksum != static_cast<uint32_t>(exp_mc->valuedouble)) {
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "scene_conflict");
            }
        }
    }

    SceneMotionConfig motion{};
    const char* m_err = nullptr;
    if (!parse_and_validate_motion_json(root, index, motion, m_err)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", m_err ? m_err : "invalid_motion");
    }

    cJSON_Delete(root);

    esp_err_t err = api_ptr->config()->save_scene_motion(index, motion);
    if (err != ESP_OK) return send_error(request, "500 Internal Server Error", "nvs_write_failed");

    // Requirement 5: Saving must NEVER alter live output.
    // Explicitly do NOT reload running scene motion.

    SceneMotionConfig verify_mc{};
    uint32_t motion_cs = 0;
    if (api_ptr->config()->load_scene_motion(index, verify_mc) == ESP_OK) {
        motion_cs = verify_mc.checksum;
    }

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::SceneMotion);
    api_ptr->command_journal().applied(cmd_id);

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"saved\":true,\"commandId\":%" PRIu32 ",\"checksum\":%" PRIu32 "}", cmd_id, motion_cs);
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
            cJSON_AddNumberToObject(obj, "endAction", static_cast<int>(cl.end_action));
            cJSON_AddStringToObject(obj, "endActionName", cue_end_action_name(cl.end_action));
            cJSON_AddNumberToObject(obj, "checksum", cl.checksum);
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
    cJSON_AddNumberToObject(obj, "schema", 2);
    cJSON_AddNumberToObject(obj, "index", index);
    cJSON_AddNumberToObject(obj, "checksum", cl.checksum);
    cJSON_AddStringToObject(obj, "name", cl.name);
    cJSON_AddStringToObject(obj, "mode", cue_run_mode_name(cl.mode));
    cJSON_AddNumberToObject(obj, "count", cl.count);
    cJSON_AddNumberToObject(obj, "endAction", static_cast<int>(cl.end_action));
    cJSON_AddStringToObject(obj, "endActionName", cue_end_action_name(cl.end_action));

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

    // ── List index: 0..7 (body or query) ──
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

    // ── Name: mandatory, 1..31 UTF-8 bytes, no silent truncation (§35) ──
    CueList cl{};
    const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!name_j || !cJSON_IsString(name_j) || name_j->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_name");
    }
    if (!is_valid_utf8(name_j->valuestring) || utf8_byte_len(name_j->valuestring) > CueList::kMaxNameBytes) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "name_too_long");
    }
    strncpy(cl.name, name_j->valuestring, sizeof(cl.name) - 1);

    // ── Mode: strict enum (§32) ──
    const cJSON* mode_j = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (!mode_j || !cJSON_IsString(mode_j) || !parse_cue_run_mode(mode_j->valuestring, cl.mode)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_run_mode");
    }

    // ── End action: strict string, or numeric 0..2 compatibility (§3/§4) ──
    const cJSON* ea_j = cJSON_GetObjectItemCaseSensitive(root, "endAction");
    if (ea_j && cJSON_IsString(ea_j)) {
        if (!parse_cue_end_action(ea_j->valuestring, cl.end_action)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_end_action");
        }
    } else if (ea_j && cJSON_IsNumber(ea_j)) {
        if (ea_j->valueint < 0 || ea_j->valueint > 2) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_end_action");
        }
        cl.end_action = static_cast<CueEndAction>(ea_j->valueint);
    } else {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_end_action");
    }

    // ── Cues: 1..16, no silent truncation (§33/§34) ──
    const cJSON* cues = cJSON_GetObjectItemCaseSensitive(root, "cues");
    if (!cues || !cJSON_IsArray(cues)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_cues");
    }
    const int cue_array_len = cJSON_GetArraySize(cues);
    if (cue_array_len < 1 || cue_array_len > kMaxCuesPerList) {
        cJSON_Delete(root);
        return send_error(request, cue_array_len > kMaxCuesPerList ? "400 Bad Request" : "400 Bad Request",
                          cue_array_len > kMaxCuesPerList ? "too_many_cues" : "empty_cue_list");
    }

    uint8_t c_idx = 0;
    const cJSON* c_item = nullptr;
    cJSON_ArrayForEach(c_item, cues) {
        auto& cue = cl.cues[c_idx];

        const cJSON* sc_j = cJSON_GetObjectItemCaseSensitive(c_item, "sceneIndex");
        if (!sc_j || !cJSON_IsNumber(sc_j) || sc_j->valueint < 0 || sc_j->valueint > 31) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_scene_index");
        }
        cue.scene_index = static_cast<uint8_t>(sc_j->valueint);

        const cJSON* hold_j = cJSON_GetObjectItemCaseSensitive(c_item, "holdMs");
        if (!hold_j || !cJSON_IsNumber(hold_j) || hold_j->valuedouble < 0 || hold_j->valuedouble > 3600000.0) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_hold_ms");
        }
        cue.hold_ms = static_cast<uint32_t>(hold_j->valuedouble);

        const cJSON* fade_j = cJSON_GetObjectItemCaseSensitive(c_item, "fadeMs");
        if (!fade_j || !cJSON_IsNumber(fade_j) || fade_j->valuedouble < 0 || fade_j->valuedouble > 60000.0) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_fade_ms");
        }
        cue.fade_ms = static_cast<uint16_t>(fade_j->valuedouble);

        const cJSON* cur_j = cJSON_GetObjectItemCaseSensitive(c_item, "curve");
        if (!cur_j || !cJSON_IsString(cur_j) || !parse_transition_curve(cur_j->valuestring, cue.curve)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_curve");
        }

        const cJSON* lbl_j = cJSON_GetObjectItemCaseSensitive(c_item, "label");
        if (lbl_j && cJSON_IsString(lbl_j)) {
            if (!is_valid_utf8(lbl_j->valuestring) || utf8_byte_len(lbl_j->valuestring) > CueEntry::kMaxLabelBytes) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "label_too_long");
            }
            strncpy(cue.label, lbl_j->valuestring, sizeof(cue.label) - 1);
        }
        c_idx++;
    }
    cl.count = c_idx;

    // ── Referenced Scenes must exist (§33) ──
    for (uint8_t i = 0; i < cl.count; ++i) {
        if (!api_ptr->config()->scene_exists(cl.cues[i].scene_index)) {
            cJSON* err_body = cJSON_CreateObject();
            cJSON_AddStringToObject(err_body, "error", "cue_scene_missing");
            cJSON_AddNumberToObject(err_body, "cueIndex", i);
            cJSON_AddNumberToObject(err_body, "sceneIndex", cl.cues[i].scene_index);
            cJSON_Delete(root);
            esp_err_t res = send_error_json(request, "400 Bad Request", err_body);
            return res;
        }
    }

    // ── Optimistic checksum conflict protection (§37) ──
    const cJSON* expected_j = cJSON_GetObjectItemCaseSensitive(root, "expectedChecksum");
    CueList current{};
    const bool exists = (api_ptr->config()->load_cue_list(index, current) == ESP_OK && current.count > 0);
    if (expected_j && cJSON_IsNumber(expected_j) && exists &&
        static_cast<uint32_t>(expected_j->valuedouble) != current.checksum) {
        cJSON* err_body = cJSON_CreateObject();
        cJSON_AddStringToObject(err_body, "error", "cue_list_conflict");
        cJSON_AddNumberToObject(err_body, "currentChecksum", current.checksum);
        cJSON_Delete(root);
        esp_err_t res = send_error_json(request, "409 Conflict", err_body);
        return res;
    }

    cJSON_Delete(root);

    esp_err_t err = api_ptr->config()->save_cue_list(index, cl);
    if (err != ESP_OK) return send_error(request, "500 Internal Server Error", "nvs_write_failed");

    // Verify readback to obtain the authoritative stored checksum (§38)
    CueList stored{};
    uint32_t stored_checksum = cl.checksum;
    if (api_ptr->config()->load_cue_list(index, stored) == ESP_OK) {
        stored_checksum = stored.checksum;
    }

    // §39: saving a running list never mutates the active run; the checksum
    // difference surfaces as contentStale in runtime telemetry.
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Cue);
    api_ptr->command_journal().applied(cmd_id);

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"saved\":true,\"checksum\":%" PRIu32 ",\"commandId\":%" PRIu32 "}",
        stored_checksum, cmd_id);
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

    // ── Reference protection (§41/§42/§43) ──
    cJSON* references = cJSON_CreateArray();

    // Active transport
    if (api_ptr->cue_runner()) {
        const auto snap = api_ptr->cue_runner()->snapshot(api_ptr->show_clock_now_ms());
        if (snap.running && snap.list_index == index) {
            cJSON* ref = cJSON_CreateObject();
            cJSON_AddStringToObject(ref, "type", "activeTransport");
            cJSON_AddItemToArray(references, ref);
        }
    }

    // Calendar Schedule references
    if (api_ptr->config()) {
        CalendarSchedule sched{};
        if (api_ptr->config()->load_calendar_schedule(sched) == ESP_OK) {
            for (uint8_t i = 0; i < 8; ++i) {
                const auto& entry = sched.entries[i];
                if (entry.enabled && entry.action == CalendarAction::CueList && entry.cue_list_index == index) {
                    cJSON* ref = cJSON_CreateObject();
                    cJSON_AddStringToObject(ref, "type", "schedule");
                    cJSON_AddNumberToObject(ref, "index", i);
                    cJSON_AddItemToArray(references, ref);
                }
            }
        }
    }

    if (cJSON_GetArraySize(references) > 0) {
        cJSON* err_body = cJSON_CreateObject();
        cJSON_AddStringToObject(err_body, "error", "cue_list_in_use");
        cJSON_AddItemToObject(err_body, "references", references);
        return send_error_json(request, "409 Conflict", err_body);
    }
    cJSON_Delete(references);

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

    // LG-034 §15: journal domain must reflect the requested transport mode
    // BEFORE dispatch. Scene/Timeline/Cue commands are no longer journaled as
    // Scene commands.
    CommandDomain command_domain = CommandDomain::Scene;
    if (strcmp(mode_str, "timeline") == 0) command_domain = CommandDomain::Timeline;
    else if (strcmp(mode_str, "cue") == 0) command_domain = CommandDomain::Cue;
    else if (strcmp(mode_str, "scene") != 0) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "unsupported_mode");
    }
    const uint32_t cmd_id = api_ptr->command_journal().accept(command_domain);

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
        if (!api_ptr->config() ||
            !api_ptr->config()->scene_exists(static_cast<uint8_t>(scene_idx))) {
            cJSON_Delete(root);
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_NOT_FOUND);
            return send_error(request, "404 Not Found", "scene_not_found");
        }

        if (api_ptr->cue_runner()) {
            api_ptr->cue_runner()->stop(CueStopReason::Preempted, now_ms);
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
        uint64_t anchor_ms = 0;
        uint32_t active_cs = 0;

        if (strcmp(action, "start") == 0 || strcmp(action, "restart") == 0) {
            uint16_t bpm = 0; // Missing override uses the admitted content's tempo.
            const cJSON* bpm_j = cJSON_GetObjectItemCaseSensitive(root, "bpm");
            if (bpm_j) {
                if (!cJSON_IsNumber(bpm_j) || bpm_j->valuedouble < 10 ||
                    bpm_j->valuedouble > 500 || bpm_j->valuedouble != bpm_j->valueint) {
                    cJSON_Delete(root);
                    api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
                    return send_error(request, "400 Bad Request", "invalid_bpm_10_500");
                }
                bpm = static_cast<uint16_t>(bpm_j->valueint);
            }
            Scheduler::TimelineError timeline_error = Scheduler::TimelineError::None;
            const esp_err_t started = api_ptr->scheduler()
                ? api_ptr->scheduler()->start_local_timeline(bpm, 0, &timeline_error)
                : ESP_ERR_INVALID_STATE;
            if (started != ESP_OK) {
                cJSON_Delete(root);
                api_ptr->command_journal().failed(cmd_id, started);
                return send_error(request,
                    started == ESP_ERR_INVALID_ARG ? "400 Bad Request" :
                    started == ESP_ERR_NOT_FOUND ? "404 Not Found" : "503 Service Unavailable",
                    timeline_error == Scheduler::TimelineError::SceneMissing ? "timeline_scene_not_found" :
                    timeline_error == Scheduler::TimelineError::Invalid ? "timeline_invalid" :
                    started == ESP_ERR_NOT_FOUND ? "timeline_not_found" : "timeline_unavailable");
            }
            uint32_t saved_cs = 0;
            if (api_ptr->scheduler()) {
                auto snap = api_ptr->scheduler()->transport_snapshot(api_ptr->show_clock_now_ms());
                bpm = snap.bpm;
                anchor_ms = snap.timeline_anchor_ms;
                transport_gen = snap.transport_generation;
                active_cs = snap.timeline_active_checksum;
                saved_cs = snap.timeline_saved_checksum;
            }
            if (api_ptr->show_transport()) {
                // Both start and restart admit a new snapshot and reset its anchor.
                api_ptr->show_transport()->set_timeline(
                    true, bpm, anchor_ms, active_cs, saved_cs, true
                );
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
        (void)now_ms;

        cJSON_Delete(root);
        api_ptr->command_journal().applied(cmd_id);
        api_ptr->notify_status_async();

        char resp[256];
        snprintf(resp, sizeof(resp),
            "{\"accepted\":true,\"mode\":\"timeline\",\"generation\":%" PRIu32 ",\"anchorMs\":%" PRIu64 ",\"checksum\":%" PRIu32 ",\"commandId\":%" PRIu32 "}",
            transport_gen, anchor_ms, active_cs, cmd_id);
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

        // LG-034 §16: every transport op returns a deterministic status;
        // invalid requests fail with 400/409 instead of silently no-oping.
        if (strcmp(action, "start") == 0) {
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
            if (api_ptr->scheduler()) {
                api_ptr->scheduler()->stop_local_timeline();
            }
        } else if (strcmp(action, "restart") == 0) {
            // LG-034 §13: canonical restart — newest saved list, fresh active
            // snapshot, new generation, no end-action transient, no blackout.
            uint8_t list_idx = 0;
            uint8_t start_cue = 0;
            const cJSON* li = cJSON_GetObjectItemCaseSensitive(root, "cueListIndex");
            if (li && cJSON_IsNumber(li)) list_idx = static_cast<uint8_t>(li->valueint);
            const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
            if (ci && cJSON_IsNumber(ci)) start_cue = static_cast<uint8_t>(ci->valueint);
            esp_err_t err = api_ptr->cue_runner()->restart(list_idx, start_cue, now_ms);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "restart_failed");
            }
        } else if (strcmp(action, "stop") == 0) {
            // §2: operator stop holds current output; never executes end action.
            api_ptr->cue_runner()->stop(CueStopReason::Operator, now_ms);
        } else if (strcmp(action, "pause") == 0) {
            esp_err_t err = api_ptr->cue_runner()->pause(now_ms);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "pause_invalid_state");
            }
        } else if (strcmp(action, "resume") == 0) {
            esp_err_t err = api_ptr->cue_runner()->resume(now_ms);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "resume_invalid_state");
            }
        } else if (strcmp(action, "next") == 0) {
            esp_err_t err = api_ptr->cue_runner()->next(now_ms);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "next_invalid_state");
            }
        } else if (strcmp(action, "previous") == 0) {
            esp_err_t err = api_ptr->cue_runner()->previous(now_ms);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "previous_invalid_state");
            }
        } else if (strcmp(action, "jump") == 0) {
            const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
            if (!ci || !cJSON_IsNumber(ci) || ci->valueint < 0 || ci->valueint > 255) {
                api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_cue_index");
            }
            esp_err_t err = api_ptr->cue_runner()->jump(static_cast<uint8_t>(ci->valueint), now_ms);
            if (err == ESP_ERR_INVALID_ARG) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_cue_index");
            }
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "jump_invalid_state");
            }
        } else if (strcmp(action, "crossfade") == 0) {
            // §17: strictly validate 0.0..1.0; reject strings, missing values,
            // and out-of-range numbers (never wrap negative into uint16_t).
            const cJSON* f_j = cJSON_GetObjectItemCaseSensitive(root, "crossfade");
            if (!f_j || !cJSON_IsNumber(f_j) || std::isnan(f_j->valuedouble) || std::isinf(f_j->valuedouble)) {
                api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_crossfade");
            }
            const double fade_val = f_j->valuedouble;
            if (fade_val < 0.0 || fade_val > 1.0) {
                api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_crossfade");
            }
            const uint16_t fade_q15 = static_cast<uint16_t>(std::lround(fade_val * 32768.0));
            esp_err_t err = api_ptr->cue_runner()->set_manual_fade(fade_q15);
            if (err != ESP_OK) {
                api_ptr->command_journal().failed(cmd_id, err);
                cJSON_Delete(root);
                return send_error(request, "409 Conflict", "crossfade_requires_manual");
            }
        } else {
            cJSON_Delete(root);
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
            return send_error(request, "400 Bad Request", "unknown_action");
        }

        uint32_t transport_gen = 0;
        if (api_ptr->show_transport()) {
            const auto cr_snap = api_ptr->cue_runner()->sync_snapshot(now_ms);
            if (cr_snap.active) {
                CueTransportSnapshot cs{};
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
        // LG-034 §22/§23: canonical cue telemetry from the CueRunner runtime
        // authority (one locked snapshot, no ShowTransportCoordinator copy).
        const auto snap = api_ptr->cue_runner()->snapshot(now_ms);
        cJSON_AddBoolToObject(cue_obj, "running", snap.running);
        cJSON_AddStringToObject(cue_obj, "state", cue_runner_state_name(snap.state));
        cJSON_AddStringToObject(cue_obj, "pausedFrom",
                                snap.state == CueRunnerState::Paused ? cue_runner_state_name(snap.paused_from) : "stopped");
        cJSON_AddNumberToObject(cue_obj, "listIndex", snap.list_index);
        cJSON_AddStringToObject(cue_obj, "mode", cue_run_mode_name(snap.mode));
        cJSON_AddNumberToObject(cue_obj, "cueIndex", snap.current_cue);
        cJSON_AddNumberToObject(cue_obj, "nextCueIndex", snap.next_cue);
        cJSON_AddNumberToObject(cue_obj, "currentSceneIndex", snap.current_scene);
        cJSON_AddNumberToObject(cue_obj, "nextSceneIndex", snap.next_scene);
        cJSON_AddNumberToObject(cue_obj, "elapsedMs", snap.elapsed_ms);
        cJSON_AddNumberToObject(cue_obj, "remainingMs", snap.remaining_ms);
        cJSON_AddNumberToObject(cue_obj, "manualFade", snap.manual_fade_q15);
        cJSON_AddNumberToObject(cue_obj, "transitionProgress", snap.transition_progress_q15);
        cJSON_AddNumberToObject(cue_obj, "generation", snap.generation);
        cJSON_AddNumberToObject(cue_obj, "activeChecksum", snap.active_checksum);
        cJSON_AddNumberToObject(cue_obj, "savedChecksum", snap.saved_checksum);
        cJSON_AddBoolToObject(cue_obj, "contentStale", snap.content_stale);
        cJSON_AddBoolToObject(cue_obj, "remoteDriven", snap.remote_driven);
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

    // LG-034 §15: deprecated compat endpoint still journals the correct domain.
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
        api_ptr->cue_runner()->stop(CueStopReason::Operator, now_ms);
    } else if (strcmp(action, "pause") == 0) {
        esp_err_t err = api_ptr->cue_runner()->pause(now_ms);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, "409 Conflict", "pause_invalid_state");
        }
    } else if (strcmp(action, "resume") == 0) {
        esp_err_t err = api_ptr->cue_runner()->resume(now_ms);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, "409 Conflict", "resume_invalid_state");
        }
    } else if (strcmp(action, "next") == 0) {
        esp_err_t err = api_ptr->cue_runner()->next(now_ms);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, "409 Conflict", "next_invalid_state");
        }
    } else if (strcmp(action, "previous") == 0) {
        esp_err_t err = api_ptr->cue_runner()->previous(now_ms);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, "409 Conflict", "previous_invalid_state");
        }
    } else if (strcmp(action, "jump") == 0) {
        uint8_t cue_idx = 0;
        const cJSON* ci = cJSON_GetObjectItemCaseSensitive(root, "cueIndex");
        if (ci && cJSON_IsNumber(ci)) cue_idx = ci->valueint;
        esp_err_t err = api_ptr->cue_runner()->jump(cue_idx, now_ms);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "409 Conflict",
                              err == ESP_ERR_INVALID_ARG ? "invalid_cue_index" : "jump_invalid_state");
        }
    } else if (strcmp(action, "crossfade") == 0) {
        // §17: strict 0..1 on the canonical path; legacy numeric tolerance is
        // intentionally NOT provided here beyond the same strict bounds.
        const cJSON* f_j = cJSON_GetObjectItemCaseSensitive(root, "crossfade");
        if (!f_j || !cJSON_IsNumber(f_j) || std::isnan(f_j->valuedouble) || std::isinf(f_j->valuedouble) ||
            f_j->valuedouble < 0.0 || f_j->valuedouble > 1.0) {
            api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_crossfade");
        }
        const uint16_t fade_q15 = static_cast<uint16_t>(std::lround(f_j->valuedouble * 32768.0));
        esp_err_t err = api_ptr->cue_runner()->set_manual_fade(fade_q15);
        if (err != ESP_OK) {
            api_ptr->command_journal().failed(cmd_id, err);
            cJSON_Delete(root);
            return send_error(request, "409 Conflict", "crossfade_requires_manual");
        }
    } else {
        cJSON_Delete(root);
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_INVALID_ARG);
        return send_error(request, "400 Bad Request", "unknown_action");
    }

    if (api_ptr->show_transport()) {
        const auto cr_snap = api_ptr->cue_runner()->sync_snapshot(now_ms);
        if (cr_snap.active) {
            CueTransportSnapshot cs{};
            cs.active = cr_snap.active;
            cs.list_index = cr_snap.list_index;
            cs.state = static_cast<uint8_t>(cr_snap.state);
            cs.current_cue = cr_snap.current_cue;
            cs.next_cue = cr_snap.next_cue;
            cs.manual_fade_q15 = cr_snap.manual_fade_q15;
            cs.cue_anchor_ms = cr_snap.cue_anchor_ms;
            cs.paused_elapsed_ms = cr_snap.paused_elapsed_ms;
            cs.generation = cr_snap.generation;
            // LG-034 v6 fields (carried on the sync snapshot)
            cs.list_checksum = cr_snap.list_checksum;
            cs.transition_progress_q15 = cr_snap.transition_progress_q15;
            cs.paused_from = static_cast<uint8_t>(cr_snap.paused_from);
            cs.source_scene = cr_snap.source_scene;
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

    const uint64_t now_ms = api_ptr->show_clock_now_ms();
    const auto snap = api_ptr->cue_runner()->snapshot(now_ms);
    char body[512];
    snprintf(body, sizeof(body),
        "{\"running\":%s,\"state\":\"%s\",\"pausedFrom\":\"%s\",\"listIndex\":%u,\"mode\":\"%s\",\"cueIndex\":%u,\"nextCueIndex\":%u,\"elapsedMs\":%" PRIu32 ",\"remainingMs\":%" PRIu32 ",\"manualFade\":%u,\"generation\":%" PRIu32 ",\"activeChecksum\":%" PRIu32 ",\"savedChecksum\":%" PRIu32 ",\"contentStale\":%s,\"remoteDriven\":%s}",
        snap.running ? "true" : "false",
        cue_runner_state_name(snap.state),
        cue_runner_state_name(snap.paused_from),
        snap.list_index,
        cue_run_mode_name(snap.mode),
        snap.current_cue,
        snap.next_cue,
        snap.elapsed_ms,
        snap.remaining_ms,
        snap.manual_fade_q15,
        snap.generation,
        snap.active_checksum,
        snap.saved_checksum,
        snap.content_stale ? "true" : "false",
        snap.remote_driven ? "true" : "false");

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

    const auto reject_activation = [&]() {
        const uint32_t command_id = api_ptr->command_journal().accept(CommandDomain::Scene, 0);
        api_ptr->command_journal().failed(command_id, ESP_ERR_INVALID_STATE);

        char detail[64];
        snprintf(detail, sizeof(detail), "Preset %u activation rejected", index);
        api::AuditLog::instance().log(api::ApiRole::Operator, "preset_activate_rejected", detail);
        return send_error(request, "409 Conflict", "scene_activation_rejected");
    };
    if (!api_ptr->config() || !api_ptr->config()->scene_exists(index)) {
        return reject_activation();
    }

    // Stop Cue if running (Scene activation preempts without end action)
    if (api_ptr->cue_runner()) {
        api_ptr->cue_runner()->stop(CueStopReason::Preempted, now_ms);
    }
    // Suppress local Timeline
    if (api_ptr->scheduler()) {
        api_ptr->scheduler()->stop_local_timeline();
        api_ptr->scheduler()->clear_sync_transport();
    }

    uint32_t generation = 0;
    if (!api_ptr->signage()->activate_scene_checked(index, &generation)) {
        return reject_activation();
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
    uint32_t checksum = ConfigStore::compute_device_crc(cfg);

    cJSON* response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "schema", 3);
    cJSON_AddNumberToObject(response, "checksum", checksum);
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
    // LG-036 §14: syncToken is strictly write-only, never returned in GET /api/v1/config
    cJSON_AddBoolToObject(response, "syncTokenSet", cfg.sync_token != 0);
    cJSON_AddBoolToObject(response, "mqttEnabled", cfg.mqtt_enabled);
    cJSON_AddStringToObject(response, "mqttBroker", cfg.mqtt_broker);
    cJSON_AddNumberToObject(response, "mqttPort", cfg.mqtt_port);

    // LG-036 §13 / LG-HW-005: single source of truth for numeric limits
    cJSON* limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(limits, "maxPowerMw", kMaxGlobalPowerMw);
    cJSON_AddNumberToObject(limits, "maxChannelPowerMw", kMaxChannelPowerMw);
    cJSON_AddNumberToObject(limits, "maxPixelsPerChannel", 512);
    cJSON_AddNumberToObject(limits, "maxGroups", 8);
    cJSON_AddNumberToObject(limits, "maxChannels", 16);
    cJSON_AddNumberToObject(limits, "maxDmxUniverse", 63999);
    cJSON_AddItemToObject(response, "limits", limits);

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
        cJSON_AddNumberToObject(ch, "channelPowerLimitMw", c.channel_limit_mw);
        cJSON_AddNumberToObject(ch, "ratedFullPowerMw", c.rated_full_power_mw);
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

    // HW-005.5: Power supply domains
    cJSON* psu_arr = cJSON_CreateArray();
    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& psu = cfg.power_supplies[s];
        cJSON* psu_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(psu_obj, "id", s);
        cJSON_AddBoolToObject(psu_obj, "enabled", psu.enabled);
        cJSON_AddNumberToObject(psu_obj, "voltageMv", psu.voltage_mv);
        cJSON_AddNumberToObject(psu_obj, "ratedCurrentMa", psu.rated_current_ma);
        cJSON_AddNumberToObject(psu_obj, "usableCapacityPermille", psu.usable_capacity_permille);
        cJSON_AddNumberToObject(psu_obj, "channelMask", psu.channel_mask);
        cJSON_AddNumberToObject(psu_obj, "ratedPowerMw", psu.rated_power_mw());
        cJSON_AddNumberToObject(psu_obj, "usablePowerMw", psu.usable_power_mw());
        cJSON_AddItemToArray(psu_arr, psu_obj);
    }
    cJSON_AddItemToObject(response, "powerSupplies", psu_arr);

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

bool parse_and_validate_device_config(const cJSON* root, const DeviceConfig& current_cfg, DeviceConfig& out_cfg, char* err_code, size_t err_len) {
    out_cfg = current_cfg;

    #define PARSE_INT_BOUNDS(json_name, min_v, max_v, target, code_on_err) \
    do { \
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, json_name); \
        if (item) { \
            if (!lg::json::is_integer(item) || item->valuedouble < (min_v) || item->valuedouble > (max_v)) { \
                snprintf(err_code, err_len, "%s", code_on_err); \
                return false; \
            } \
            target = static_cast<decltype(target)>(item->valuedouble); \
        } \
    } while(0)

    #define PARSE_STRING_BOUNDS(json_name, max_len, target, code_on_err) \
    do { \
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, json_name); \
        if (item) { \
            if (!cJSON_IsString(item) || strlen(item->valuestring) >= (max_len)) { \
                snprintf(err_code, err_len, "%s", code_on_err); \
                return false; \
            } \
            strncpy(target, item->valuestring, (max_len) - 1); \
            target[(max_len) - 1] = '\0'; \
        } \
    } while(0)

    PARSE_STRING_BOUNDS("deviceName", sizeof(out_cfg.device_name), out_cfg.device_name, "invalid_field_deviceName");
    PARSE_STRING_BOUNDS("serialNumber", sizeof(out_cfg.serial_number), out_cfg.serial_number, "invalid_field_serialNumber");
    PARSE_STRING_BOUNDS("hardwareRevision", sizeof(out_cfg.hardware_revision), out_cfg.hardware_revision, "invalid_field_hardwareRevision");
    PARSE_STRING_BOUNDS("siteLabel", sizeof(out_cfg.site_label), out_cfg.site_label, "invalid_field_siteLabel");
    PARSE_STRING_BOUNDS("groupLabel", sizeof(out_cfg.group_label), out_cfg.group_label, "invalid_field_groupLabel");

    const cJSON* ap = cJSON_GetObjectItemCaseSensitive(root, "activePreset");
    if (!ap) ap = cJSON_GetObjectItemCaseSensitive(root, "activePresetIndex");
    if (ap) {
        if (!lg::json::is_integer(ap) || ap->valuedouble < 0 || ap->valuedouble > (kMaxPresets - 1)) {
            snprintf(err_code, err_len, "invalid_field_activePreset");
            return false;
        }
        out_cfg.active_preset_index = static_cast<uint8_t>(ap->valueint);
    }

    const cJSON* fp = cJSON_GetObjectItemCaseSensitive(root, "fallbackPreset");
    if (!fp) fp = cJSON_GetObjectItemCaseSensitive(root, "fallbackPresetIndex");
    if (fp) {
        if (!lg::json::is_integer(fp) || fp->valuedouble < 0 || fp->valuedouble > (kMaxPresets - 1)) {
            snprintf(err_code, err_len, "invalid_field_fallbackPreset");
            return false;
        }
        out_cfg.fallback_preset_index = static_cast<uint8_t>(fp->valueint);
    }

    PARSE_INT_BOUNDS("watchdogTimeoutS", 0, 3600, out_cfg.watchdog_timeout_s, "invalid_field_watchdogTimeoutS");
    PARSE_INT_BOUNDS("maxPowerMw", 0, kMaxGlobalPowerMw, out_cfg.max_power_mw, "invalid_field_maxPowerMw");
    PARSE_INT_BOUNDS("softStartMs", 0, 5000, out_cfg.soft_start_ms, "invalid_field_softStartMs");

    const cJSON* gm = cJSON_GetObjectItemCaseSensitive(root, "gamma");
    if (!gm) gm = cJSON_GetObjectItemCaseSensitive(root, "gammaX10");
    if (gm) {
        if (!cJSON_IsNumber(gm) || !std::isfinite(gm->valuedouble)) {
            snprintf(err_code, err_len, "invalid_field_gamma");
            return false;
        }
        if (lg::json::is_integer(gm) && gm->valuedouble >= 10.0 && gm->valuedouble <= 30.0) {
            out_cfg.gamma_x10 = static_cast<uint8_t>(gm->valueint);
        } else if (gm->valuedouble >= 1.0 && gm->valuedouble <= 3.0) {
            out_cfg.gamma_x10 = static_cast<uint8_t>(std::round(gm->valuedouble * 10.0));
        } else {
            snprintf(err_code, err_len, "invalid_field_gamma");
            return false;
        }
    }

    const cJSON* ss = cJSON_GetObjectItemCaseSensitive(root, "softStart");
    if (!ss) ss = cJSON_GetObjectItemCaseSensitive(root, "softStartEnabled");
    if (ss) {
        if (!cJSON_IsBool(ss)) {
            snprintf(err_code, err_len, "invalid_field_softStart");
            return false;
        }
        out_cfg.soft_start_enabled = cJSON_IsTrue(ss);
    }

    PARSE_STRING_BOUNDS("timezone", sizeof(out_cfg.timezone), out_cfg.timezone, "invalid_field_timezone");

    const cJSON* se = cJSON_GetObjectItemCaseSensitive(root, "syncEnabled");
    if (se) {
        if (!cJSON_IsBool(se)) {
            snprintf(err_code, err_len, "invalid_field_syncEnabled");
            return false;
        }
        out_cfg.sync_enabled = cJSON_IsTrue(se);
    }

    PARSE_INT_BOUNDS("syncRole", 0, 2, out_cfg.sync_role, "invalid_field_syncRole");

    // Sync token write-only semantics (LG-036 §14)
    const cJSON* clear_st = cJSON_GetObjectItemCaseSensitive(root, "clearSyncToken");
    if (clear_st && cJSON_IsTrue(clear_st)) {
        out_cfg.sync_token = 0;
    } else {
        const cJSON* st = cJSON_GetObjectItemCaseSensitive(root, "syncToken");
        if (st && !cJSON_IsNull(st)) {
            if (!lg::json::is_integer(st) || st->valuedouble < 0 || st->valuedouble > 4294967295.0) {
                snprintf(err_code, err_len, "invalid_field_syncToken");
                return false;
            }
            out_cfg.sync_token = static_cast<uint32_t>(st->valuedouble);
        }
        // If omitted or null, out_cfg retains current_cfg.sync_token
    }

    const cJSON* me = cJSON_GetObjectItemCaseSensitive(root, "mqttEnabled");
    if (me) {
        if (!cJSON_IsBool(me)) {
            snprintf(err_code, err_len, "invalid_field_mqttEnabled");
            return false;
        }
        out_cfg.mqtt_enabled = cJSON_IsTrue(me);
    }
    PARSE_STRING_BOUNDS("mqttBroker", sizeof(out_cfg.mqtt_broker), out_cfg.mqtt_broker, "invalid_field_mqttBroker");
    PARSE_INT_BOUNDS("mqttPort", 1, 65535, out_cfg.mqtt_port, "invalid_field_mqttPort");

    // Channel array validation (LG-036 §9, §10)
    const cJSON* channels_arr = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (channels_arr) {
        if (!cJSON_IsArray(channels_arr)) {
            snprintf(err_code, err_len, "invalid_channels_format");
            return false;
        }
        int arr_size = cJSON_GetArraySize(channels_arr);
        if (arr_size > 16) {
            snprintf(err_code, err_len, "too_many_channels");
            return false;
        }
        int ch_idx = 0;
        cJSON* ch_item = nullptr;
        cJSON_ArrayForEach(ch_item, channels_arr) {
            if (!cJSON_IsObject(ch_item)) {
                snprintf(err_code, err_len, "invalid_channel_item");
                return false;
            }
            const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(ch_item, "name");
            if (name_j) {
                if (!cJSON_IsString(name_j) || strlen(name_j->valuestring) >= sizeof(out_cfg.channels[ch_idx].name)) {
                    snprintf(err_code, err_len, "invalid_field_channel_name");
                    return false;
                }
                strncpy(out_cfg.channels[ch_idx].name, name_j->valuestring, sizeof(out_cfg.channels[ch_idx].name) - 1);
                out_cfg.channels[ch_idx].name[sizeof(out_cfg.channels[ch_idx].name) - 1] = '\0';
            }
            const cJSON* en_j = cJSON_GetObjectItemCaseSensitive(ch_item, "enabled");
            if (en_j) {
                if (!cJSON_IsBool(en_j)) {
                    snprintf(err_code, err_len, "invalid_field_channel_enabled");
                    return false;
                }
                out_cfg.channels[ch_idx].enabled = cJSON_IsTrue(en_j);
            }
            const cJSON* mb_j = cJSON_GetObjectItemCaseSensitive(ch_item, "maxBrightness");
            if (mb_j) {
                if (!lg::json::is_integer(mb_j) || mb_j->valuedouble < 0 || mb_j->valuedouble > 255) {
                    snprintf(err_code, err_len, "invalid_field_maxBrightness");
                    return false;
                }
                out_cfg.channels[ch_idx].max_brightness = static_cast<uint8_t>(mb_j->valueint);
            }
            const cJSON* gc_j = cJSON_GetObjectItemCaseSensitive(ch_item, "gammaCorrection");
            if (gc_j) {
                if (!cJSON_IsBool(gc_j)) {
                    snprintf(err_code, err_len, "invalid_field_gammaCorrection");
                    return false;
                }
                out_cfg.channels[ch_idx].gamma_correction = cJSON_IsTrue(gc_j);
            }
            const cJSON* mw_j = cJSON_GetObjectItemCaseSensitive(ch_item, "maxMilliwatts");
            if (mw_j) {
                if (!lg::json::is_integer(mw_j) || mw_j->valuedouble < 0 || mw_j->valuedouble > kMaxChannelPowerMw) {
                    snprintf(err_code, err_len, "invalid_field_maxMilliwatts");
                    return false;
                }
                out_cfg.channels[ch_idx].max_milliwatts = static_cast<uint32_t>(mw_j->valuedouble);
                out_cfg.channels[ch_idx].channel_limit_mw = static_cast<uint32_t>(mw_j->valuedouble);
                if (ch_idx >= 8) {
                    out_cfg.channels[ch_idx].rated_full_power_mw = static_cast<uint32_t>(mw_j->valuedouble);
                }
            }
            const cJSON* clim_j = cJSON_GetObjectItemCaseSensitive(ch_item, "channelPowerLimitMw");
            if (clim_j) {
                if (!lg::json::is_integer(clim_j) || clim_j->valuedouble < 0 || clim_j->valuedouble > kMaxChannelPowerMw) {
                    snprintf(err_code, err_len, "invalid_field_channelPowerLimitMw");
                    return false;
                }
                out_cfg.channels[ch_idx].channel_limit_mw = static_cast<uint32_t>(clim_j->valuedouble);
            }
            const cJSON* rfp_j = cJSON_GetObjectItemCaseSensitive(ch_item, "ratedFullPowerMw");
            if (rfp_j) {
                if (!lg::json::is_integer(rfp_j) || rfp_j->valuedouble < 0 || rfp_j->valuedouble > kMaxChannelPowerMw) {
                    snprintf(err_code, err_len, "invalid_field_ratedFullPowerMw");
                    return false;
                }
                out_cfg.channels[ch_idx].rated_full_power_mw = static_cast<uint32_t>(rfp_j->valuedouble);
            }
            const cJSON* du_j = cJSON_GetObjectItemCaseSensitive(ch_item, "dmxUniverse");
            if (du_j) {
                if (!lg::json::is_integer(du_j) || du_j->valuedouble < 0 || du_j->valuedouble > 63999) {
                    snprintf(err_code, err_len, "invalid_field_dmxUniverse");
                    return false;
                }
                out_cfg.channels[ch_idx].dmx_universe = static_cast<uint16_t>(du_j->valueint);
            }
            const cJSON* ds_j = cJSON_GetObjectItemCaseSensitive(ch_item, "dmxStartChannel");
            if (ds_j) {
                if (!lg::json::is_integer(ds_j) || ds_j->valuedouble < 0 || ds_j->valuedouble > 512) {
                    snprintf(err_code, err_len, "invalid_field_dmxStartChannel");
                    return false;
                }
                out_cfg.channels[ch_idx].dmx_start_channel = static_cast<uint16_t>(ds_j->valueint);
            }
            const cJSON* dm_j = cJSON_GetObjectItemCaseSensitive(ch_item, "dmxMode");
            if (dm_j) {
                if (!lg::json::is_integer(dm_j) || dm_j->valuedouble < 0 || dm_j->valuedouble > 2) {
                    snprintf(err_code, err_len, "invalid_field_dmxMode");
                    return false;
                }
                out_cfg.channels[ch_idx].dmx_mode = static_cast<uint8_t>(dm_j->valueint);
            }

            // Addressable configuration for channels 0..7 (A1..A8)
            const cJSON* addr_obj = cJSON_GetObjectItemCaseSensitive(ch_item, "addressable");
            const cJSON* src_addr = addr_obj ? addr_obj : ch_item;
            const cJSON* atype_j = cJSON_GetObjectItemCaseSensitive(src_addr, "type");
            if (!atype_j) atype_j = cJSON_GetObjectItemCaseSensitive(src_addr, "addressableType");
            if (atype_j) {
                if (ch_idx < 8) {
                    if (cJSON_IsString(atype_j)) {
                        if (strcmp(atype_j->valuestring, "SK6812") == 0) out_cfg.channels[ch_idx].addressable_type = AddressableType::SK6812;
                        else if (strcmp(atype_j->valuestring, "WS2812") == 0) out_cfg.channels[ch_idx].addressable_type = AddressableType::WS2812;
                        else {
                            snprintf(err_code, err_len, "invalid_field_addressableType");
                            return false;
                        }
                    } else if (lg::json::is_integer(atype_j) && (atype_j->valueint == 0 || atype_j->valueint == 1)) {
                        out_cfg.channels[ch_idx].addressable_type = static_cast<AddressableType>(atype_j->valueint);
                    } else {
                        snprintf(err_code, err_len, "invalid_field_addressableType");
                        return false;
                    }
                }
            }
            const cJSON* px_j = cJSON_GetObjectItemCaseSensitive(src_addr, "pixels");
            if (!px_j) px_j = cJSON_GetObjectItemCaseSensitive(src_addr, "pixelCount");
            if (px_j) {
                if (ch_idx < 8) {
                    if (!lg::json::is_integer(px_j) || px_j->valuedouble < 0 || px_j->valuedouble > MAX_PIXELS) {
                        snprintf(err_code, err_len, "invalid_field_pixelCount");
                        return false;
                    }
                    out_cfg.channels[ch_idx].pixel_count = static_cast<uint16_t>(px_j->valueint);
                }
            }
            const cJSON* po_j = cJSON_GetObjectItemCaseSensitive(src_addr, "pixelOrder");
            if (po_j) {
                if (ch_idx < 8) {
                    if (lg::json::is_integer(po_j)) {
                        if (po_j->valueint < 0 || po_j->valueint > 3) {
                            snprintf(err_code, err_len, "invalid_field_pixelOrder");
                            return false;
                        }
                        out_cfg.channels[ch_idx].pixel_order = static_cast<PixelOrder>(po_j->valueint);
                    } else if (cJSON_IsString(po_j)) {
                        if (!parse_pixel_order(po_j->valuestring, out_cfg.channels[ch_idx].pixel_order)) {
                            snprintf(err_code, err_len, "invalid_field_pixelOrder");
                            return false;
                        }
                    } else {
                        snprintf(err_code, err_len, "invalid_field_pixelOrder");
                        return false;
                    }
                }
            }

            ++ch_idx;
        }
    }

    // Groups array validation (LG-036 §10, §12)
    const cJSON* groups_arr = cJSON_GetObjectItemCaseSensitive(root, "groups");
    if (groups_arr) {
        if (!cJSON_IsArray(groups_arr)) {
            snprintf(err_code, err_len, "invalid_groups_format");
            return false;
        }
        int arr_size = cJSON_GetArraySize(groups_arr);
        if (arr_size > 8) {
            snprintf(err_code, err_len, "too_many_groups");
            return false;
        }
        int g_idx = 0;
        cJSON* g_item = nullptr;
        cJSON_ArrayForEach(g_item, groups_arr) {
            if (!cJSON_IsObject(g_item)) {
                snprintf(err_code, err_len, "invalid_group_item");
                return false;
            }
            const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(g_item, "name");
            if (name_j) {
                if (!cJSON_IsString(name_j) || strlen(name_j->valuestring) >= sizeof(out_cfg.groups[g_idx].name)) {
                    snprintf(err_code, err_len, "invalid_field_group_name");
                    return false;
                }
                strncpy(out_cfg.groups[g_idx].name, name_j->valuestring, sizeof(out_cfg.groups[g_idx].name) - 1);
                out_cfg.groups[g_idx].name[sizeof(out_cfg.groups[g_idx].name) - 1] = '\0';
            }
            const cJSON* mask_j = cJSON_GetObjectItemCaseSensitive(g_item, "channelMask");
            if (mask_j) {
                if (!lg::json::is_integer(mask_j) || mask_j->valuedouble < 0 || mask_j->valuedouble > 0xFFFF) {
                    snprintf(err_code, err_len, "invalid_field_channelMask");
                    return false;
                }
                out_cfg.groups[g_idx].channel_mask = static_cast<uint16_t>(mask_j->valueint);
            }
            const cJSON* en_j = cJSON_GetObjectItemCaseSensitive(g_item, "enabled");
            if (en_j) {
                if (!cJSON_IsBool(en_j)) {
                    snprintf(err_code, err_len, "invalid_field_group_enabled");
                    return false;
                }
                out_cfg.groups[g_idx].enabled = cJSON_IsTrue(en_j);
            }
            const cJSON* role_j = cJSON_GetObjectItemCaseSensitive(g_item, "role");
            if (role_j) {
                if (cJSON_IsString(role_j)) {
                    if (!parse_zone_role(role_j->valuestring, out_cfg.groups[g_idx].role)) {
                        snprintf(err_code, err_len, "invalid_field_group_role");
                        return false;
                    }
                } else if (lg::json::is_integer(role_j)) {
                    if (role_j->valueint < 0 || role_j->valueint > 4) {
                        snprintf(err_code, err_len, "invalid_field_group_role");
                        return false;
                    }
                    out_cfg.groups[g_idx].role = static_cast<ZoneRole>(role_j->valueint);
                } else {
                    snprintf(err_code, err_len, "invalid_field_group_role");
                    return false;
                }
            }
            ++g_idx;
        }
    }

    // Power supplies validation (LG-HW-005.5)
    const cJSON* psus = cJSON_GetObjectItemCaseSensitive(root, "powerSupplies");
    if (psus) {
        if (!cJSON_IsArray(psus)) {
            snprintf(err_code, err_len, "invalid_field_powerSupplies");
            return false;
        }
        int psu_size = cJSON_GetArraySize(psus);
        if (psu_size > kMaxPowerSupplies) {
            snprintf(err_code, err_len, "too_many_power_supplies");
            return false;
        }
        for (int i = 0; i < psu_size; ++i) {
            const cJSON* p_item = cJSON_GetArrayItem(psus, i);
            if (!cJSON_IsObject(p_item)) {
                snprintf(err_code, err_len, "invalid_field_powerSupplies");
                return false;
            }
            const cJSON* id_j = cJSON_GetObjectItemCaseSensitive(p_item, "id");
            if (!id_j || !lg::json::is_integer(id_j) || id_j->valueint < 0 || id_j->valueint >= kMaxPowerSupplies) {
                snprintf(err_code, err_len, "invalid_field_powerSupplies");
                return false;
            }
            uint8_t p_id = static_cast<uint8_t>(id_j->valueint);
            auto& psu = out_cfg.power_supplies[p_id];

            const cJSON* name_j = cJSON_GetObjectItemCaseSensitive(p_item, "name");
            if (name_j) {
                if (!cJSON_IsString(name_j) || !name_j->valuestring ||
                    std::strlen(name_j->valuestring) >= sizeof(psu.name)) {
                    snprintf(err_code, err_len, "invalid_supply_name");
                    return false;
                }
                lg::json::safe_strncpy(psu.name, name_j->valuestring, sizeof(psu.name));
            }

            const cJSON* en_j = cJSON_GetObjectItemCaseSensitive(p_item, "enabled");
            if (en_j) {
                if (!cJSON_IsBool(en_j)) {
                    snprintf(err_code, err_len, "invalid_supply_enabled");
                    return false;
                }
                psu.enabled = cJSON_IsTrue(en_j);
            }

            const cJSON* v_j = cJSON_GetObjectItemCaseSensitive(p_item, "voltageMv");
            if (v_j) {
                if (!lg::json::is_integer(v_j) || (v_j->valuedouble < (psu.enabled ? 3000 : 0) || v_j->valuedouble > 60000)) {
                    snprintf(err_code, err_len, "invalid_supply_voltage");
                    return false;
                }
                psu.voltage_mv = static_cast<uint32_t>(v_j->valuedouble);
            }

            const cJSON* cur_j = cJSON_GetObjectItemCaseSensitive(p_item, "ratedCurrentMa");
            if (cur_j) {
                if (!lg::json::is_integer(cur_j) || (cur_j->valuedouble < (psu.enabled ? 1 : 0) || cur_j->valuedouble > 200000)) {
                    snprintf(err_code, err_len, "invalid_current_rating");
                    return false;
                }
                psu.rated_current_ma = static_cast<uint32_t>(cur_j->valuedouble);
            }

            const cJSON* cap_j = cJSON_GetObjectItemCaseSensitive(p_item, "usableCapacityPermille");
            if (cap_j) {
                if (!lg::json::is_integer(cap_j) || (cap_j->valuedouble < (psu.enabled ? 1 : 0) || cap_j->valuedouble > 1000)) {
                    snprintf(err_code, err_len, "invalid_field_usableCapacityPermille");
                    return false;
                }
                psu.usable_capacity_permille = static_cast<uint16_t>(cap_j->valueint);
            }

            const cJSON* mask_j = cJSON_GetObjectItemCaseSensitive(p_item, "channelMask");
            if (mask_j) {
                if (!lg::json::is_integer(mask_j) || mask_j->valuedouble < 0 || mask_j->valuedouble > 0xFFFF) {
                    snprintf(err_code, err_len, "invalid_field_channelMask");
                    return false;
                }
                psu.channel_mask = static_cast<uint16_t>(mask_j->valueint);
            }
        }

        // Validate channel mask conflicts across enabled supplies
        for (uint8_t a = 0; a < kMaxPowerSupplies; ++a) {
            if (!out_cfg.power_supplies[a].enabled) continue;
            for (uint8_t b = a + 1; b < kMaxPowerSupplies; ++b) {
                if (!out_cfg.power_supplies[b].enabled) continue;
                if ((out_cfg.power_supplies[a].channel_mask & out_cfg.power_supplies[b].channel_mask) != 0) {
                    snprintf(err_code, err_len, "supply_channel_assignment_conflict");
                    return false;
                }
            }
        }
    }

    // Protocol config validation (LG-036 §11)
    const cJSON* prot = cJSON_GetObjectItemCaseSensitive(root, "protocol");
    if (prot) {
        if (!cJSON_IsObject(prot)) {
            snprintf(err_code, err_len, "invalid_protocol_format");
            return false;
        }
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(prot, "artnetEnabled");
        if (item) {
            if (!cJSON_IsBool(item)) { snprintf(err_code, err_len, "invalid_field_artnetEnabled"); return false; }
            out_cfg.protocol.artnet_enabled = cJSON_IsTrue(item);
        }
        item = cJSON_GetObjectItemCaseSensitive(prot, "sacnEnabled");
        if (item) {
            if (!cJSON_IsBool(item)) { snprintf(err_code, err_len, "invalid_field_sacnEnabled"); return false; }
            out_cfg.protocol.sacn_enabled = cJSON_IsTrue(item);
        }
        item = cJSON_GetObjectItemCaseSensitive(prot, "universeStart");
        if (item) {
            if (!lg::json::is_integer(item) || item->valuedouble < 0 || item->valuedouble > 63999) {
                snprintf(err_code, err_len, "invalid_field_universeStart");
                return false;
            }
            out_cfg.protocol.universe_start = static_cast<uint16_t>(item->valueint);
        }
        item = cJSON_GetObjectItemCaseSensitive(prot, "mergeMode");
        if (item) {
            if (cJSON_IsString(item) && strcmp(item->valuestring, "LTP") == 0) out_cfg.protocol.merge_mode = MergeMode::LTP;
            else if (cJSON_IsString(item) && strcmp(item->valuestring, "HTP") == 0) out_cfg.protocol.merge_mode = MergeMode::HTP;
            // Previously exported v4 packages encoded this enum numerically.
            else if (lg::json::is_integer(item) && (item->valuedouble == 0 || item->valuedouble == 1))
                out_cfg.protocol.merge_mode = static_cast<MergeMode>(item->valueint);
            else { snprintf(err_code, err_len, "invalid_field_mergeMode"); return false; }
        }
        item = cJSON_GetObjectItemCaseSensitive(prot, "streamTimeoutMs");
        if (item) {
            if (!lg::json::is_integer(item) || item->valuedouble < 100 || item->valuedouble > 60000) {
                snprintf(err_code, err_len, "invalid_field_streamTimeoutMs");
                return false;
            }
            out_cfg.protocol.stream_timeout_ms = static_cast<uint16_t>(item->valueint);
        }
        item = cJSON_GetObjectItemCaseSensitive(prot, "liveStreamLayer");
        if (item) {
            if (!lg::json::is_integer(item) || item->valuedouble < 0 || item->valuedouble > 2) {
                snprintf(err_code, err_len, "invalid_field_liveStreamLayer");
                return false;
            }
            out_cfg.protocol.live_stream_layer = static_cast<uint8_t>(item->valueint);
        }
        item = cJSON_GetObjectItemCaseSensitive(prot, "zoneStartChannel");
        if (item) {
            if (!lg::json::is_integer(item) || item->valuedouble < 0 || item->valuedouble > 512) {
                snprintf(err_code, err_len, "invalid_field_zoneStartChannel");
                return false;
            }
            out_cfg.protocol.zone_start_channel = static_cast<uint16_t>(item->valueint);
        }
    }

    #undef PARSE_INT_BOUNDS
    #undef PARSE_STRING_BOUNDS
    return true;
}

esp_err_t post_config_validate(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    DeviceConfig current_cfg{};
    api_ptr->config()->load_config(current_cfg);
    uint32_t current_crc = ConfigStore::compute_device_crc(current_cfg);

    DeviceConfig candidate_cfg{};
    char err_buf[64] = "";
    if (!parse_and_validate_device_config(root, current_cfg, candidate_cfg, err_buf, sizeof(err_buf))) {
        cJSON_Delete(root);
        cJSON* err_resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(err_resp, "valid", false);
        cJSON_AddStringToObject(err_resp, "error", err_buf);
        return send_error_json(request, "400 Bad Request", err_resp);
    }

    ConfigApplyPlan plan = diff_device_config(current_cfg, candidate_cfg);
    cJSON_Delete(root);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "valid", true);
    cJSON_AddNumberToObject(resp, "currentChecksum", current_crc);

    cJSON* impact = cJSON_CreateObject();
    cJSON_AddBoolToObject(impact, "hotApply", (plan.impact == ConfigApplyImpact::None || plan.impact == ConfigApplyImpact::Metadata || plan.impact == ConfigApplyImpact::LightingHot));
    cJSON_AddBoolToObject(impact, "outputRestart", plan.requires_output_reconfigure());
    cJSON_AddBoolToObject(impact, "dmxRestart", plan.requires_dmx_reconfigure());
    cJSON_AddBoolToObject(impact, "syncRestart", plan.requires_sync_restart());
    cJSON_AddBoolToObject(impact, "schedulerRestart", plan.requires_scheduler_restart());
    cJSON_AddBoolToObject(impact, "mqttRestart", plan.requires_mqtt_restart());
    cJSON_AddBoolToObject(impact, "rebootRequired", false);
    cJSON_AddItemToObject(resp, "impact", impact);

    cJSON* warnings = cJSON_CreateArray();
    if (candidate_cfg.max_power_mw == 0) {
        cJSON_AddItemToArray(warnings, cJSON_CreateString("no_power_limit_configured"));
    }
    if (!api_ptr->config()->scene_exists(candidate_cfg.fallback_preset_index)) {
        cJSON_AddItemToArray(warnings, cJSON_CreateString("fallback_scene_not_found"));
    }

    // HW-005.16: Preflight diagnostics and warnings
    bool any_pwm_incomplete = false;
    for (uint8_t ch = 8; ch < 16; ++ch) {
        if (candidate_cfg.channels[ch].enabled && power_model::pwm_rating(candidate_cfg, ch) == 0) {
            any_pwm_incomplete = true;
            break;
        }
    }
    if (any_pwm_incomplete) {
        cJSON_AddItemToArray(warnings, cJSON_CreateString("power_model_incomplete"));
    }

    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& psu = candidate_cfg.power_supplies[s];
        if (!psu.enabled) continue;
        uint64_t supply_chan_sum = 0;
        for (uint8_t ch = 0; ch < 16; ++ch) {
            if ((psu.channel_mask & (1u << ch)) != 0 && candidate_cfg.channels[ch].enabled) {
                if (ch < 8) {
                    supply_chan_sum += static_cast<uint64_t>(candidate_cfg.channels[ch].pixel_count) *
                        power_model::full_pixel_mw(candidate_cfg.channels[ch].addressable_type == AddressableType::SK6812);
                } else {
                    supply_chan_sum += power_model::pwm_rating(candidate_cfg, ch);
                }
            }
        }
        if (supply_chan_sum > psu.usable_power_mw()) {
            cJSON_AddItemToArray(warnings, cJSON_CreateString("supply_capacity_exceeded"));
            break;
        }
    }

    if (candidate_cfg.schema_version < 4 || (candidate_cfg.max_power_mw <= 60000 && candidate_cfg.max_power_mw > 0)) {
        uint32_t active_pixels = 0;
        for (uint8_t ch = 0; ch < 8; ++ch) {
            if (candidate_cfg.channels[ch].enabled) active_pixels += candidate_cfg.channels[ch].pixel_count;
        }
        if (active_pixels > 512) {
            cJSON_AddItemToArray(warnings, cJSON_CreateString("legacy_power_limit_migration_required"));
        }
    }

    // LG-038.3 / LG-HW-002: output configuration impact preview.
    // Real internal SRAM and generic heap admission model.
    {
        const OutputMemoryAdmission admission = check_output_memory_admission(candidate_cfg);
        if (admission.generic_required > 0 || admission.internal_required > 0) {
            char warn[160];
            snprintf(warn, sizeof(warn),
                     "topology_req_internal:%u_generic:%u_freeInternal:%u_reserveInternal:%u",
                     (unsigned)admission.internal_required, (unsigned)admission.generic_required,
                     (unsigned)admission.free_internal, (unsigned)admission.internal_reserve);
            cJSON_AddItemToArray(warnings, cJSON_CreateString(warn));
            if (!admission.accepted) {
                cJSON_AddBoolToObject(resp, "valid", false);
                cJSON_AddStringToObject(resp, "error", "insufficient_runtime_memory");
                cJSON_AddStringToObject(resp, "reason", admission.rejection_reason ? admission.rejection_reason : "unknown");
                cJSON_AddNumberToObject(resp, "requiredGeneric", admission.generic_required);
                cJSON_AddNumberToObject(resp, "requiredInternal", admission.internal_required);
                cJSON_AddNumberToObject(resp, "free8", admission.free_8bit);
                cJSON_AddNumberToObject(resp, "freeInternal", admission.free_internal);
                cJSON_AddNumberToObject(resp, "largestInternal", admission.largest_internal);
                cJSON_AddNumberToObject(resp, "reserveGeneric", admission.generic_reserve);
                cJSON_AddNumberToObject(resp, "reserveInternal", admission.internal_reserve);
            }
        }
    }
    cJSON_AddItemToObject(resp, "warnings", warnings);

    return send_json(request, resp);
}

esp_err_t put_device_config(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) return send_error(request, "503 Service Unavailable", "config_not_ready");

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    DeviceConfig old_cfg{};
    api_ptr->config()->load_config(old_cfg);
    uint32_t current_crc = ConfigStore::compute_device_crc(old_cfg);

    const cJSON* exp = cJSON_GetObjectItemCaseSensitive(root, "expectedChecksum");
    if (exp && cJSON_IsNumber(exp)) {
        uint32_t expected = static_cast<uint32_t>(exp->valuedouble);
        if (expected != current_crc) {
            cJSON_Delete(root);
            cJSON* conflict = cJSON_CreateObject();
            cJSON_AddStringToObject(conflict, "error", "config_conflict");
            cJSON_AddNumberToObject(conflict, "currentChecksum", current_crc);
            return send_error_json(request, "409 Conflict", conflict);
        }
    }

    DeviceConfig candidate_cfg{};
    char err_buf[64] = "";
    if (!parse_and_validate_device_config(root, old_cfg, candidate_cfg, err_buf, sizeof(err_buf))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", err_buf);
    }
    cJSON_Delete(root);

    ConfigApplyPlan plan = diff_device_config(old_cfg, candidate_cfg);
    if (plan.requires_output_reconfigure()) {
        const OutputMemoryAdmission admission = check_output_memory_admission(candidate_cfg);
        if (!admission.accepted) {
            return send_output_admission_error(request, admission);
        }
    }
    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Config);

    // Apply candidate to runtime first (LG-036 §18, §19)
    if (api_ptr->runtime_apply_func()) {
        esp_err_t apply_err = api_ptr->runtime_apply_func()(old_cfg, candidate_cfg, plan);
        if (apply_err != ESP_OK) {
            if (apply_err == kErrInsufficientRuntimeMemory) {
                api_ptr->command_journal().failed(cmd_id, apply_err);
                const OutputMemoryAdmission adm = check_output_memory_admission(candidate_cfg);
                return send_output_admission_error(request, adm);
            }
            api_ptr->runtime_apply_func()(candidate_cfg, old_cfg, plan); // rollback
            api_ptr->command_journal().failed(cmd_id, apply_err);
            api::AuditLog::instance().log(api::ApiRole::Admin, "config_apply_failed", "Runtime apply failed, rolled back");
            return send_error(request, "500 Internal Server Error", "config_apply_failed");
        }
    } else if (api_ptr->config_apply_callback()) {
        esp_err_t apply_err = api_ptr->config_apply_callback()(old_cfg, candidate_cfg);
        if (apply_err != ESP_OK) {
            api_ptr->config_apply_callback()(candidate_cfg, old_cfg);
            api_ptr->command_journal().failed(cmd_id, apply_err);
            return send_error(request, "500 Internal Server Error", "sync_reconfig_failed");
        }
    }

    // Runtime apply succeeded, now persist candidate (LG-036 §18)
    esp_err_t save_err = api_ptr->config()->save_config(candidate_cfg);
    if (save_err != ESP_OK) {
        // Rollback runtime to old_cfg!
        if (api_ptr->runtime_apply_func()) {
            api_ptr->runtime_apply_func()(candidate_cfg, old_cfg, plan);
        } else if (api_ptr->config_apply_callback()) {
            api_ptr->config_apply_callback()(candidate_cfg, old_cfg);
        }
        api_ptr->command_journal().failed(cmd_id, save_err);
        api::AuditLog::instance().log(api::ApiRole::Admin, "config_persist_failed", "Persistence failed, runtime rolled back");
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    // Read back and verify checksum
    api_ptr->config()->load_config(old_cfg);
    uint32_t resulting_crc = ConfigStore::compute_device_crc(old_cfg);

    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();
    api::AuditLog::instance().log(api::ApiRole::Admin, "config_apply", "Config updated");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "accepted", true);
    cJSON_AddBoolToObject(resp, "saved", true);
    cJSON_AddNumberToObject(resp, "commandId", cmd_id);
    cJSON_AddNumberToObject(resp, "checksum", resulting_crc);
    cJSON_AddStringToObject(resp, "apply", plan.requires_output_reconfigure() ? "output_reconfigure" : (plan.impact == ConfigApplyImpact::None ? "none" : "hot"));
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
    cJSON_AddBoolToObject(response, "apPasswordSet", (net_settings.ap_password[0] != '\0'));

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

    // apPassword validation (8..63 chars)
    const cJSON* ap_pwd_item = cJSON_GetObjectItemCaseSensitive(root, "apPassword");
    if (ap_pwd_item) {
        if (!cJSON_IsString(ap_pwd_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_ap_password");
        }
        const char* ap_pwd_str = ap_pwd_item->valuestring;
        if (ap_pwd_str != nullptr && ap_pwd_str[0] != '\0') {
            size_t len = std::strlen(ap_pwd_str);
            if (len < 8 || len > 63) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_ap_password");
            }
            std::strncpy(desired.ap_password, ap_pwd_str, sizeof(desired.ap_password) - 1);
            desired.ap_password[sizeof(desired.ap_password) - 1] = '\0';
        }
    }

    // AP modes require a valid password
    if ((desired.mode == NetworkMode::ApOnly || desired.mode == NetworkMode::StaWithApFallback) &&
        desired.ap_password[0] == '\0') {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_ap_password");
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

        const WifiDiagnostics wdiag = api_ptr->wifi()->diagnostics();
        const char* actual_mode_str = "null";
        switch (wdiag.actual_mode) {
            case WIFI_MODE_STA: actual_mode_str = "sta"; break;
            case WIFI_MODE_AP: actual_mode_str = "ap"; break;
            case WIFI_MODE_APSTA: actual_mode_str = "apsta"; break;
            default: actual_mode_str = "null"; break;
        }
        cJSON_AddStringToObject(response, "actualMode", actual_mode_str);
        cJSON_AddNumberToObject(response, "startAttempts", wdiag.start_attempts);
        cJSON_AddNumberToObject(response, "startFailures", wdiag.start_failures);
        cJSON_AddNumberToObject(response, "reconnectAttempts", wdiag.reconnect_attempts);
        cJSON_AddNumberToObject(response, "reconnectFailures", wdiag.reconnect_failures);
        cJSON_AddNumberToObject(response, "fallbackActivations", wdiag.fallback_activations);
        cJSON_AddNumberToObject(response, "fallbackExitAttempts", wdiag.fallback_exit_attempts);
        cJSON_AddNumberToObject(response, "fallbackExitFailures", wdiag.fallback_exit_failures);
        cJSON_AddNumberToObject(response, "disconnectEvents", wdiag.disconnect_events);
        cJSON_AddNumberToObject(response, "lastError", wdiag.last_error);

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

struct RuntimeSnapshotScratch {
    RuntimeSnapshot snapshot{};
    std::array<char, kRuntimeSnapshotMaxBytes> payload{};
};

esp_err_t get_signage_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);

    constexpr uint32_t required = sizeof(RuntimeSnapshotScratch);
    if (!check_memory_admission(required).accepted) {
        return send_memory_admission_error(request, required);
    }

    auto scratch = std::unique_ptr<RuntimeSnapshotScratch>(new (std::nothrow) RuntimeSnapshotScratch{});
    if (!scratch) {
        note_api_allocation_failure();
        return send_error(request, "503 Service Unavailable", "runtime_snapshot_memory_unavailable");
    }

    // Schema 3 Canonical RuntimeSnapshot: HTTP uses streamSeq = 0
    scratch->snapshot = collect_runtime_snapshot(api_ptr, 0);

    FixedJsonWriter writer(scratch->payload.data(), scratch->payload.size());
    if (!serialize_runtime_snapshot(scratch->snapshot, writer)) {
        return send_error(request, "500 Internal Server Error", "snapshot_serialization_failed");
    }

    // Stack and memory telemetry on authenticated /status (first call & every 20 invocations)
    static uint32_t s_status_invocations = 0;
    if (++s_status_invocations == 1 || (s_status_invocations % 20) == 0) {
        UBaseType_t stack_free = uxTaskGetStackHighWaterMark(nullptr);
        const RuntimeMemorySnapshot mem = capture_runtime_memory();
        ESP_LOGI("httpd", "HTTPD status: stackFree=%lu free8=%lu largest8=%lu payload=%u",
                 (unsigned long)(stack_free * sizeof(StackType_t)),
                 (unsigned long)mem.free_8bit,
                 (unsigned long)mem.largest_8bit,
                 (unsigned)strlen(scratch->payload.data()));
    }

    return send_json(request, scratch->payload.data());
}

esp_err_t get_dmx_frame(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Installer)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    struct DmxScratch {
        char dmx_hex[1025];
        char body[1152];
    };
    auto scratch = std::unique_ptr<DmxScratch>(new (std::nothrow) DmxScratch{});
    if (!scratch) {
        note_api_allocation_failure();
        return send_error(request, "503 Service Unavailable", "dmx_scratch_memory_unavailable");
    }

    if (api_ptr->signage()) {
        const auto& dmx = api_ptr->signage()->last_dmx_data();
        for (int i = 0; i < 512; ++i) {
            snprintf(scratch->dmx_hex + i * 2, 3, "%02x", dmx[i]);
        }
    } else {
        memset(scratch->dmx_hex, '0', 1024);
    }
    scratch->dmx_hex[1024] = '\0';

    snprintf(scratch->body, sizeof(scratch->body), "{\"universe\":1,\"dataHex\":\"%s\"}", scratch->dmx_hex);
    return send_json(request, scratch->body);
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
    if (api_ptr->certification_service()) {
        auto cs = api_ptr->certification_service()->get_status_summary();
        cJSON* cert_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(cert_obj, "state", cs.is_stale ? "stale" : certification_state_to_string(cs.state));
        cJSON_AddStringToObject(cert_obj, "certificationId", api_ptr->certification_service()->session().identity.certification_id);
        cJSON_AddNumberToObject(cert_obj, "planVersion", api_ptr->certification_service()->session().identity.plan_version);
        cJSON_AddStringToObject(cert_obj, "firmwareVersion", api_ptr->certification_service()->session().identity.firmware_version);
        cJSON_AddItemToObject(self_obj, "certification", cert_obj);
    }
    cJSON_AddItemToObject(root, "self", self_obj);

    // Discovered nodes
    cJSON* nodes_arr = cJSON_CreateArray();
    if (api_ptr->fleet_discovery()) {
        using FleetSnapshot = std::array<discovery::DiscoveredNode, discovery::FleetDiscoveryService::kMaxDiscoveredNodes>;
        constexpr uint32_t required = sizeof(FleetSnapshot);
        if (!check_memory_admission(required).accepted) {
            cJSON_Delete(root);
            return send_memory_admission_error(request, required);
        }

        auto snap = std::unique_ptr<FleetSnapshot>(new (std::nothrow) FleetSnapshot{});
        if (!snap) {
            cJSON_Delete(root);
            note_api_allocation_failure();
            return send_error(request, "503 Service Unavailable", "fleet_snapshot_memory_unavailable");
        }

        size_t n = api_ptr->fleet_discovery()->get_snapshot(snap->data(), snap->size(), now_ms);

        for (size_t i = 0; i < n; ++i) {
            const auto& item = (*snap)[i];
            cJSON* node_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(node_obj, "id", item.id);
            cJSON_AddStringToObject(node_obj, "name", item.name);
            cJSON_AddStringToObject(node_obj, "hostname", item.hostname);

            char ip_str[16];
            discovery::FleetDiscoveryService::format_ipv4(item.ipv4, ip_str);
            cJSON_AddStringToObject(node_obj, "ip", ip_str);
            cJSON_AddNumberToObject(node_obj, "port", item.port);
            cJSON_AddStringToObject(node_obj, "family", item.family);
            cJSON_AddStringToObject(node_obj, "model", item.model);
            cJSON_AddStringToObject(node_obj, "firmware", item.firmware);

            uint32_t age_ms = (now_ms >= item.last_seen_ms) ? (now_ms - item.last_seen_ms) : 0;
            cJSON_AddNumberToObject(node_obj, "lastSeenMs", item.last_seen_ms);
            cJSON_AddNumberToObject(node_obj, "ageMs", age_ms);

            discovery::NodePresence presence = discovery::FleetDiscoveryService::determine_presence(item.last_seen_ms, now_ms);
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
    auto body = std::unique_ptr<std::array<char, 1536>>(new (std::nothrow) std::array<char, 1536>());
    if (!body) {
        note_api_allocation_failure();
        return send_error(request, "503 Service Unavailable", "out_of_memory");
    }
    snprintf(body->data(), body->size(),
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
    return send_json(request, body->data());
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

    Sequence seq{};
    api_ptr->config()->load_sequence(seq);

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "schema", 2);
    cJSON_AddNumberToObject(obj, "checksum", seq.checksum);
    cJSON_AddNumberToObject(obj, "bpm", seq.bpm > 0 ? seq.bpm : 120);

    cJSON* tracks = cJSON_CreateArray();
    for (uint8_t i = 0; i < 16; ++i) {
        cJSON* trk = cJSON_CreateObject();
        cJSON_AddNumberToObject(trk, "channelId", i);
        cJSON_AddNumberToObject(trk, "numSteps", seq.tracks[i].num_steps);
        cJSON_AddBoolToObject(trk, "enabled", seq.tracks[i].enabled);

        cJSON* steps = cJSON_CreateArray();
        for (uint8_t s = 0; s < seq.tracks[i].num_steps && s < 16; ++s) {
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
    if (read_json(request, &root) != ESP_OK || !root) {
        return send_error(request, "400 Bad Request", "invalid_json");
    }

    // LG-033 Requirement 15: Optimistic concurrency / conflict protection
    Sequence current_seq{};
    api_ptr->config()->load_sequence(current_seq);
    const cJSON* exp_cs = cJSON_GetObjectItemCaseSensitive(root, "expectedChecksum");
    if (exp_cs) {
        if (!cJSON_IsNumber(exp_cs)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_expected_checksum");
        }
        uint32_t expected = static_cast<uint32_t>(exp_cs->valuedouble);
        if (expected != current_seq.checksum) {
            cJSON_Delete(root);
            char conflict_body[128];
            snprintf(conflict_body, sizeof(conflict_body),
                     "{\"error\":\"timeline_conflict\",\"currentChecksum\":%" PRIu32 "}",
                     current_seq.checksum);
            return send_json(request, conflict_body, "409 Conflict");
        }
    }

    // Strict validation: BPM 10..500
    const cJSON* bpm_j = cJSON_GetObjectItemCaseSensitive(root, "bpm");
    if (!bpm_j || !cJSON_IsNumber(bpm_j) || bpm_j->valueint < 10 || bpm_j->valueint > 500) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_bpm_10_500");
    }
    uint16_t bpm = static_cast<uint16_t>(bpm_j->valueint);

    // Strict validation: Exactly 16 canonical physical tracks
    const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (!tracks || !cJSON_IsArray(tracks) || cJSON_GetArraySize(tracks) != 16) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "requires_16_tracks");
    }

    Sequence seq{};
    seq.bpm = bpm;
    seq.enabled = false; // LG-033: always false in new saves

    for (int i = 0; i < 16; ++i) {
        const cJSON* trk_item = cJSON_GetArrayItem(tracks, i);
        if (!trk_item || !cJSON_IsObject(trk_item)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_track_object");
        }

        // Canonical mapping: channelId must match track row i
        const cJSON* ch_id_j = cJSON_GetObjectItemCaseSensitive(trk_item, "channelId");
        if (!ch_id_j || !cJSON_IsNumber(ch_id_j) || ch_id_j->valueint != i) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_channel_mapping");
        }
        seq.tracks[i].channel_id = static_cast<uint8_t>(i);

        const cJSON* trk_en = cJSON_GetObjectItemCaseSensitive(trk_item, "enabled");
        seq.tracks[i].enabled = (trk_en && cJSON_IsBool(trk_en)) ? cJSON_IsTrue(trk_en) : false;

        const cJSON* num_steps_j = cJSON_GetObjectItemCaseSensitive(trk_item, "numSteps");
        if (!num_steps_j || !cJSON_IsNumber(num_steps_j) || num_steps_j->valueint < 0 || num_steps_j->valueint > 16) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "invalid_num_steps_0_16");
        }
        uint8_t num_steps = static_cast<uint8_t>(num_steps_j->valueint);
        seq.tracks[i].num_steps = num_steps;

        const cJSON* steps = cJSON_GetObjectItemCaseSensitive(trk_item, "steps");
        if (num_steps > 0) {
            if (!steps || !cJSON_IsArray(steps) || cJSON_GetArraySize(steps) < num_steps) {
                cJSON_Delete(root);
                return send_error(request, "400 Bad Request", "invalid_steps_length");
            }

            for (uint8_t s = 0; s < num_steps; ++s) {
                const cJSON* stp_item = cJSON_GetArrayItem(steps, s);
                if (!stp_item || !cJSON_IsObject(stp_item)) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "invalid_step_object");
                }

                const cJSON* p_idx_j = cJSON_GetObjectItemCaseSensitive(stp_item, "presetIndex");
                if (!p_idx_j || !cJSON_IsNumber(p_idx_j) || p_idx_j->valueint < 0 || p_idx_j->valueint >= kMaxPresets) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "invalid_preset_index_0_31");
                }
                uint8_t preset_index = static_cast<uint8_t>(p_idx_j->valueint);

                const cJSON* len_j = cJSON_GetObjectItemCaseSensitive(stp_item, "lengthBeats");
                if (!len_j || !cJSON_IsNumber(len_j) || len_j->valueint < 1 || len_j->valueint > 64) {
                    cJSON_Delete(root);
                    return send_error(request, "400 Bad Request", "invalid_length_beats_1_64");
                }
                uint8_t length_beats = static_cast<uint8_t>(len_j->valueint);

                // LG-033 Requirement 19: Active Scene references must exist on node
                if (!api_ptr->config()->scene_exists(preset_index)) {
                    cJSON_Delete(root);
                    char err_resp[160];
                    snprintf(err_resp, sizeof(err_resp),
                        "{\"error\":\"timeline_scene_missing\",\"channel\":%d,\"step\":%d,\"presetIndex\":%d}",
                        i, s, preset_index);
                    return send_json(request, err_resp, "400 Bad Request");
                }

                seq.tracks[i].steps[s].preset_index = preset_index;
                seq.tracks[i].steps[s].length_beats = length_beats;
            }
        }
    }

    cJSON_Delete(root);

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Timeline);
    if (api_ptr->config()->save_sequence(seq) != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, ESP_ERR_NVS_NOT_FOUND);
        return send_error(request, "500 Internal Server Error", "nvs_write_failed");
    }

    // Refresh persisted sequence in config store to retrieve newly computed checksum
    api_ptr->config()->load_sequence(seq);

    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    api::AuditLog::instance().log(api::ApiRole::Operator, "sequence_update", "Sequence updated");

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"saved\":true,\"checksum\":%" PRIu32 ",\"commandId\":%" PRIu32 "}",
        seq.checksum, cmd_id);
    return send_json(request, resp, "200 OK");
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

    // Encode one entry at a time. Growing the catalogue must not grow the peak
    // JSON heap alongside Wi-Fi, RMT and scene playback buffers.
    char json[640];
    for (size_t i = 0; i < kSignageEffectCount; ++i) {
        const EffectMetadata& meta = signage_effect_metadata(static_cast<SignageEffect>(i));
        cJSON* item = cJSON_CreateObject();
        bool valid = item &&
            cJSON_AddStringToObject(item, "id", meta.id) &&
            cJSON_AddStringToObject(item, "label", meta.label) &&
            cJSON_AddStringToObject(item, "description", meta.description) &&
            cJSON_AddBoolToObject(item, "supportsSpeed", meta.supports_speed) &&
            cJSON_AddBoolToObject(item, "supportsSecondaryColor", meta.supports_secondary_color) &&
            cJSON_AddBoolToObject(item, "supportsPalette", meta.supports_palette) &&
            cJSON_AddBoolToObject(item, "spatial", meta.spatial) &&
            cJSON_AddBoolToObject(item, "pwmRecommended", meta.pwm_recommended) &&
            cJSON_AddBoolToObject(item, "hasParameter", meta.has_parameter);
        if (meta.has_parameter) {
            valid = valid &&
                cJSON_AddStringToObject(item, "parameterLabel", meta.parameter_label) &&
                cJSON_AddStringToObject(item, "parameterUnit", meta.parameter_unit) &&
                cJSON_AddNumberToObject(item, "parameterMin", meta.parameter_min) &&
                cJSON_AddNumberToObject(item, "parameterMax", meta.parameter_max) &&
                cJSON_AddNumberToObject(item, "parameterDefault", meta.parameter_default);
        }
        valid = valid && cJSON_PrintPreallocated(item, json, sizeof(json), false);
        cJSON_Delete(item);
        if (!valid) {
            note_api_allocation_failure();
            // Once a chunk is sent, fail the connection instead of completing a
            // truncated catalogue or appending a second JSON response.
            return i == 0 ? send_error(request, "500 Internal Server Error", "out_of_memory")
                          : ESP_ERR_NO_MEM;
        }
        if (i == 0) {
            httpd_resp_set_status(request, "200 OK");
            httpd_resp_set_type(request, "application/json");
            httpd_resp_set_hdr(request, "Cache-Control", "no-store");
            httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
            httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id");
            httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        }
        esp_err_t result = httpd_resp_send_chunk(request, i == 0 ? "[" : ",", 1);
        if (result != ESP_OK) return result;
        result = httpd_resp_send_chunk(request, json, strlen(json));
        if (result != ESP_OK) return result;
    }
    const esp_err_t result = httpd_resp_send_chunk(request, "]", 1);
    return result == ESP_OK ? httpd_resp_send_chunk(request, nullptr, 0) : result;
}

esp_err_t get_palettes(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    char json[128];
    for (size_t i = 0; i < kSignagePaletteCount; ++i) {
        const auto& meta = signage_palette_metadata(static_cast<uint8_t>(i));
        cJSON* item = cJSON_CreateObject();
        bool valid = item &&
            cJSON_AddNumberToObject(item, "id", meta.id) &&
            cJSON_AddStringToObject(item, "label", meta.name);
        if (i == 0) valid = valid && cJSON_AddBoolToObject(item, "usesSceneColors", true);
        valid = valid && cJSON_PrintPreallocated(item, json, sizeof(json), false);
        cJSON_Delete(item);
        if (!valid) {
            note_api_allocation_failure();
            return i == 0 ? send_error(request, "500 Internal Server Error", "out_of_memory")
                          : ESP_ERR_NO_MEM;
        }
        if (i == 0) {
            httpd_resp_set_status(request, "200 OK");
            httpd_resp_set_type(request, "application/json");
            httpd_resp_set_hdr(request, "Cache-Control", "no-store");
            httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
            httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id");
            httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        }
        esp_err_t result = httpd_resp_send_chunk(request, i == 0 ? "[" : ",", 1);
        if (result != ESP_OK) return result;
        result = httpd_resp_send_chunk(request, json, strlen(json));
        if (result != ESP_OK) return result;
    }
    const esp_err_t result = httpd_resp_send_chunk(request, "]", 1);
    return result == ESP_OK ? httpd_resp_send_chunk(request, nullptr, 0) : result;
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
    cJSON_AddNumberToObject(response, "protocolVersion", CapabilitiesManifest::kProtocolSync);
    cJSON_AddNumberToObject(response, "syncProtocol", CapabilitiesManifest::kProtocolSync);

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

    // LG-034: use the single authoritative error-name mapping so newer codes
    // (timeline_mismatch=7, cue_list_mismatch=9) surface instead of "none".
    cJSON_AddStringToObject(response, "transportError", sync_transport_error_str(status.transport_error));

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
    // Bind a browser/fleet update to the physical node selected by the operator.
    // Reject an address reused by another node before reading or writing the image.
    const size_t expected_id_length = httpd_req_get_hdr_value_len(request, "X-LumiGrid-Node-Id");
    if (expected_id_length > 0) {
        char expected_id[sizeof(api_ptr->identity().id)]{};
        if (expected_id_length >= sizeof(expected_id) ||
            httpd_req_get_hdr_value_str(request, "X-LumiGrid-Node-Id", expected_id, sizeof(expected_id)) != ESP_OK)
            return send_error(request, "400 Bad Request", "invalid_expected_node_id");
        if (strcmp(expected_id, api_ptr->identity().id) != 0)
            return send_error(request, "409 Conflict", "node_identity_mismatch");
    }
    auto& verifier = OtaBootVerifier::instance();

    if (verifier.is_pending_verify()) {
        return send_error(request, "409 Conflict", "ota_verification_pending");
    }

    if (api_ptr->current_maintenance_mode() != MaintenanceMode::None) {
        return send_error(request, "409 Conflict", "maintenance_job_active");
    }

    if (request->content_len <= 0) {
        return send_error(request, "400 Bad Request", "empty_payload");
    }

    // LG-038.3 §43/§44: OTA knows its own scratch requirement (1 KB stream
    // buffer + protocol/TLS headroom) and must preserve the post-operation
    // reserve. Do not enter the operation if admission fails halfway-safe.
    {
        const MemoryAdmissionResult admission =
            check_memory_admission(2048 /* ota scratch */);
        if (!admission.accepted) {
            char err_body[192];
            snprintf(err_body, sizeof(err_body),
                     "{\"error\":\"insufficient_runtime_memory\",\"requiredBytes\":%u,\"freeBytes\":%u,\"reserveBytes\":%u}",
                     (unsigned)admission.required_bytes,
                     (unsigned)capture_runtime_memory().free_8bit,
                     (unsigned)admission.reserve_bytes);
            httpd_resp_set_status(request, "507 Insufficient Storage");
            httpd_resp_set_type(request, "application/json");
            return httpd_resp_sendstr(request, err_body);
        }
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        return send_error(request, "500 Internal Server Error", "No OTA partition found");
    }

    // Validate OTA request size before writing (§51)
    if (request->content_len > update_partition->size) {
        char err_body[128];
        snprintf(err_body, sizeof(err_body),
                 "{\"error\":\"firmware_too_large\",\"firmwareBytes\":%u,\"maxFirmwareBytes\":%" PRIu32 "}",
                 (unsigned)request->content_len, update_partition->size);
        httpd_resp_set_status(request, "413 Payload Too Large");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, err_body);
    }

    const auto storage = capture_storage();
    if (storage.health != StorageHealth::Healthy)
        return send_error(request, "409 Conflict", storage_health_name(storage.health));
    if (request->content_len < kOtaDescriptorPrefixBytes)
        return send_error(request, "400 Bad Request", "invalid_image_descriptor");
    uint8_t prefix[kOtaDescriptorPrefixBytes];
    size_t prefix_received = 0;
    unsigned prefix_timeouts = 0;
    while (prefix_received < sizeof(prefix)) {
        const int received = httpd_req_recv(request, reinterpret_cast<char*>(prefix) + prefix_received, sizeof(prefix) - prefix_received);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++prefix_timeouts < 3) continue;
        if (received <= 0) return send_error(request, "400 Bad Request", "ota_receive_failed");
        prefix_timeouts = 0;
        prefix_received += received;
    }
    esp_app_desc_t candidate{};
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    if (!ota_image_prefix_valid(prefix, sizeof(prefix), chip.revision, &candidate))
        return send_error(request, "400 Bad Request", "invalid_image_descriptor");

    uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Ota);
    uint32_t job_id = 0;
    esp_err_t job_err = api_ptr->job_manager().start_job(JobType::Ota, &job_id);
    if (job_err != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, job_err);
        return send_error(request, "409 Conflict", "job_already_running");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Preparing, 5);

    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota", "esp_ota_begin failed (%s)", esp_err_to_name(err));
        api_ptr->job_manager().fail_job(job_id, err, "OTA begin failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "OTA begin failed");
    }

    err = esp_ota_write(update_handle, prefix, sizeof(prefix));
    if (err != ESP_OK) {
        esp_ota_abort(update_handle);
        api_ptr->job_manager().fail_job(job_id, err, "OTA prefix write failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "ota_write_failed");
    }
    char buf[512];
    int received = 0;
    size_t bytes_written = sizeof(prefix);
    size_t remaining = request->content_len - bytes_written;
    const size_t total = request->content_len;
    unsigned timeouts = 0;

    while (remaining > 0) {
        if ((received = httpd_req_recv(request, buf, std::min(remaining, sizeof(buf)))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
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
        timeouts = 0;
        bytes_written += static_cast<size_t>(received);
        remaining -= received;
        uint8_t pct = static_cast<uint8_t>(5 + (bytes_written * 85 / total));
        api_ptr->job_manager().update_progress(job_id, JobState::Running, pct);
    }

    api_ptr->job_manager().update_progress(job_id, JobState::Verifying, 95);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        api_ptr->job_manager().fail_job(job_id, err, "OTA end failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "OTA end failed");
    }

    // esp_ota_end validates the entire partition. Sequential writes may leave
    // an old valid image tail in untouched sectors, so validate again with the
    // actual uploaded byte extent. IDF includes checksum padding, optional
    // appended digest and configured signature data in this bound.
    const esp_partition_pos_t uploaded_extent{update_partition->address, static_cast<uint32_t>(bytes_written)};
    esp_image_metadata_t image_metadata{};
    err = esp_image_verify(ESP_IMAGE_VERIFY, &uploaded_extent, &image_metadata);
    if (err != ESP_OK || image_metadata.image_len > bytes_written) {
        if (err == ESP_OK) err = ESP_ERR_INVALID_SIZE;
        api_ptr->job_manager().fail_job(job_id, err, "Incomplete OTA image");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "400 Bad Request", "incomplete_ota_image");
    }

    // Validate OTA image descriptor (§52)
    err = esp_ota_get_partition_description(update_partition, &candidate);
    if (err != ESP_OK || !ota_app_descriptor_valid(candidate)) {
        if (err == ESP_OK) err = ESP_ERR_INVALID_ARG;
        api_ptr->job_manager().fail_job(job_id, err, "Invalid project descriptor");
        api_ptr->command_journal().failed(cmd_id, err);
        api::AuditLog::instance().log(api::ApiRole::Admin, "ota_failed", "Invalid project descriptor or unknown project");
        return send_error(request, "400 Bad Request", "invalid_project_descriptor");
    }

    // Commit intent before selecting the next boot partition. A journal error
    // must never leave an untracked candidate selected for the next restart.
    PersistedOtaState ota_st{};
    ota_st.state = OtaBootState::PendingReboot;
    strncpy(ota_st.previous_version, verifier.running_version(), sizeof(ota_st.previous_version) - 1);
    strncpy(ota_st.target_version, candidate.version, sizeof(ota_st.target_version) - 1);
    strncpy(ota_st.target_partition, update_partition->label, sizeof(ota_st.target_partition) - 1);
    ota_st.last_reason[0] = 0;
    ota_st.last_error = 0;
    ota_st.candidate_boot_ms = 0;
    ota_st.verification_started_ms = 0;
    ota_st.verification_deadline_ms = 0;
    err = verifier.save_state(ota_st);
    if (err != ESP_OK) {
        api_ptr->job_manager().fail_job(job_id, err, "OTA journal persistence failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "ota_state_persistence_failed");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        // A selector error is not proof of a rollback or a reboot-ready image.
        // Preserve the actual error even if recording that result also fails.
        verifier.record_update_failure("boot_selection_failed", err);
        api_ptr->job_manager().fail_job(job_id, err, "OTA set boot failed");
        api_ptr->command_journal().failed(cmd_id, err);
        return send_error(request, "500 Internal Server Error", "OTA set boot failed");
    }

    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();

    api::AuditLog::instance().log(api::ApiRole::Admin, "ota_start", "OTA image accepted, reboot pending");

    cJSON* response = cJSON_CreateObject();
    if (response && cJSON_AddBoolToObject(response, "accepted", true) &&
        cJSON_AddNumberToObject(response, "commandId", cmd_id) &&
        cJSON_AddNumberToObject(response, "jobId", job_id) &&
        cJSON_AddStringToObject(response, "nodeId", api_ptr->identity().id) &&
        cJSON_AddStringToObject(response, "targetVersion", candidate.version) &&
        cJSON_AddStringToObject(response, "targetPartition", update_partition->label) &&
        cJSON_AddStringToObject(response, "projectName", candidate.project_name) &&
        cJSON_AddStringToObject(response, "state", "pending_reboot")) {
        send_json(request, response);
    } else {
        cJSON_Delete(response);
        note_api_allocation_failure();
        // The durable operation has succeeded even if the detailed response
        // cannot be allocated. Keep its outcome truthful and still restart.
        send_json(request, "{\"accepted\":true,\"state\":\"pending_reboot\"}");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t get_ota_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    const auto& verifier = OtaBootVerifier::instance();
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        note_api_allocation_failure();
        return send_error(request, "503 Service Unavailable", "out_of_memory");
    }
    const auto persisted = verifier.state();
    const auto storage = capture_storage();
    cJSON_AddStringToObject(root, "nodeId", api_ptr->identity().id);
    cJSON_AddBoolToObject(root, "targetBinding", true);
    cJSON_AddNumberToObject(root, "maxFirmwareBytes", storage.next ? storage.next->size : 0);
    cJSON_AddStringToObject(root, "runningPartition", storage.running ? storage.running->label : "unknown");
    cJSON_AddStringToObject(root, "nextPartition", storage.next ? storage.next->label : "unknown");
    cJSON_AddNumberToObject(root, "partitionLayout", kPartitionLayoutVersion);
    cJSON_AddNumberToObject(root, "imageBytes", storage.image_bytes);
    cJSON_AddNumberToObject(root, "verificationStartedMs", persisted.verification_started_ms);
    cJSON_AddNumberToObject(root, "verificationDeadlineMs", persisted.verification_deadline_ms);
    cJSON_AddNumberToObject(root, "candidateBootMs", persisted.candidate_boot_ms);
    cJSON_AddStringToObject(root, "state", ota_boot_state_to_name(persisted.state));
    cJSON_AddStringToObject(root, "runningVersion", verifier.running_version());
    cJSON_AddStringToObject(root, "previousVersion", persisted.previous_version);
    cJSON_AddStringToObject(root, "targetVersion", persisted.target_version);
    cJSON_AddStringToObject(root, "targetPartition", persisted.target_partition);
    cJSON_AddBoolToObject(root, "pendingVerification", verifier.is_pending_verify());
    cJSON_AddNumberToObject(root, "lastError", persisted.last_error);
    cJSON_AddNumberToObject(root, "persistenceError", verifier.persistence_error());
    cJSON_AddBoolToObject(root, "rolledBack", persisted.state == OtaBootState::RolledBack);
    if (strlen(persisted.last_reason) > 0) {
        cJSON_AddStringToObject(root, "reason", persisted.last_reason);
    }

    return send_json(request, root);
}

esp_err_t get_recovery_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    RecoveryStatus st{};
    if (api_ptr->recovery_status_provider()) {
        st = api_ptr->recovery_status_provider()();
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "safeMode", st.safe_mode);
    cJSON_AddBoolToObject(root, "configValid", st.config_valid);
    // CommissioningState: map enum to string inline (no helper exists in auth namespace)
    const char* cs_name = "none";
    if (st.commissioning_state == auth::CommissioningState::FactoryFirstBoot) cs_name = "factory_first_boot";
    else if (st.commissioning_state == auth::CommissioningState::PhysicalRecovery) cs_name = "physical_recovery";
    cJSON_AddStringToObject(root, "commissioningState", cs_name);
    cJSON_AddNumberToObject(root, "startupState", st.startup_state);
    cJSON_AddNumberToObject(root, "failingStage", st.failing_stage);
    cJSON_AddNumberToObject(root, "failingError", st.failing_error);
    cJSON_AddBoolToObject(root, "recoveryNetworkActive", st.recovery_network_active);
    cJSON_AddBoolToObject(root, "outputsForcedOff", st.outputs_forced_off);
    cJSON_AddStringToObject(root, "otaState", ota_boot_state_to_name(st.ota_state));
    cJSON_AddStringToObject(root, "restoreState", restore_state_to_name(st.restore_state));

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t res = send_json(request, json_str);
    cJSON_free(json_str);
    return res;
}

// ── Backup v4 & Restore Domain Serializers & Parsers ──

class BackupJsonBuilder {
public:
    cJSON* object() { cJSON* value = cJSON_CreateObject(); if (!value) ok_ = false; return value; }
    cJSON* array() { cJSON* value = cJSON_CreateArray(); if (!value) ok_ = false; return value; }
    void string(cJSON* parent, const char* name, const char* value) {
        if (!cJSON_AddStringToObject(parent, name, value)) ok_ = false;
    }
    void number(cJSON* parent, const char* name, double value) {
        if (!cJSON_AddNumberToObject(parent, name, value)) ok_ = false;
    }
    void boolean(cJSON* parent, const char* name, bool value) {
        if (!cJSON_AddBoolToObject(parent, name, value)) ok_ = false;
    }
    void item(cJSON* parent, const char* name, cJSON* value) {
        if (!value || !cJSON_AddItemToObject(parent, name, value)) {
            ok_ = false;
            cJSON_Delete(value);
        }
    }
    void item(cJSON* parent, cJSON* value) {
        if (!value || !cJSON_AddItemToArray(parent, value)) {
            ok_ = false;
            cJSON_Delete(value);
        }
    }
    cJSON* finish(cJSON* root) {
        if (!ok_) { cJSON_Delete(root); return nullptr; }
        return root;
    }
private:
    bool ok_ = true;
};

static cJSON* serialize_device_config(const DeviceConfig& cfg) {
    BackupJsonBuilder json;
    cJSON* cfg_json = json.object();
    json.string(cfg_json, "deviceName", cfg.device_name);
    json.string(cfg_json, "timezone", cfg.timezone);
    json.string(cfg_json, "siteLabel", cfg.site_label);
    json.string(cfg_json, "groupLabel", cfg.group_label);
    json.number(cfg_json, "activePresetIndex", cfg.active_preset_index);
    json.number(cfg_json, "fallbackPresetIndex", cfg.fallback_preset_index);
    json.number(cfg_json, "watchdogTimeoutS", cfg.watchdog_timeout_s);
    json.number(cfg_json, "maxPowerMw", cfg.max_power_mw);
    json.number(cfg_json, "gammaX10", cfg.gamma_x10);
    json.boolean(cfg_json, "softStartEnabled", cfg.soft_start_enabled);
    json.number(cfg_json, "softStartMs", cfg.soft_start_ms);
    json.boolean(cfg_json, "mqttEnabled", cfg.mqtt_enabled);
    json.string(cfg_json, "mqttBroker", cfg.mqtt_broker);
    json.number(cfg_json, "mqttPort", cfg.mqtt_port);
    json.boolean(cfg_json, "syncEnabled", cfg.sync_enabled);
    json.number(cfg_json, "syncRole", cfg.sync_role);
    // Write-only sync token indicator (§14, §27) - token itself NEVER exported!
    json.boolean(cfg_json, "syncTokenSet", cfg.sync_token != 0);

    // Protocol config
    cJSON* prot_json = json.object();
    json.boolean(prot_json, "artnetEnabled", cfg.protocol.artnet_enabled);
    json.boolean(prot_json, "sacnEnabled", cfg.protocol.sacn_enabled);
    json.number(prot_json, "universeStart", cfg.protocol.universe_start);
    json.number(prot_json, "liveStreamLayer", cfg.protocol.live_stream_layer);
    json.string(prot_json, "mergeMode", cfg.protocol.merge_mode == MergeMode::LTP ? "LTP" : "HTP");
    json.number(prot_json, "streamTimeoutMs", cfg.protocol.stream_timeout_ms);
    json.number(prot_json, "zoneStartChannel", cfg.protocol.zone_start_channel);
    json.item(cfg_json, "protocol", prot_json);

    // Channels
    cJSON* channels_arr = json.array();
    for (uint8_t c = 0; c < 16; ++c) {
        const auto& ch = cfg.channels[c];
        cJSON* cj = json.object();
        json.number(cj, "index", c);
        json.string(cj, "name", ch.name);
        json.boolean(cj, "enabled", ch.enabled);
        json.number(cj, "addressableType", static_cast<int>(ch.addressable_type));
        json.number(cj, "pixelOrder", static_cast<int>(ch.pixel_order));
        json.number(cj, "pixelCount", ch.pixel_count);
        json.number(cj, "maxBrightness", ch.max_brightness);
        json.boolean(cj, "gammaCorrection", ch.gamma_correction);
        json.number(cj, "maxMilliwatts", ch.max_milliwatts);
        json.number(cj, "channelPowerLimitMw", ch.channel_limit_mw);
        json.number(cj, "ratedFullPowerMw", ch.rated_full_power_mw);
        json.number(cj, "dmxUniverse", ch.dmx_universe);
        json.number(cj, "dmxStartChannel", ch.dmx_start_channel);
        json.number(cj, "dmxMode", ch.dmx_mode);
        json.item(channels_arr, cj);
    }
    json.item(cfg_json, "channels", channels_arr);

    // Power supplies (HW-005.5)
    cJSON* psu_arr = json.array();
    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& psu = cfg.power_supplies[s];
        cJSON* pj = json.object();
        json.number(pj, "id", s);
        json.string(pj, "name", psu.name);
        json.boolean(pj, "enabled", psu.enabled);
        json.number(pj, "voltageMv", psu.voltage_mv);
        json.number(pj, "ratedCurrentMa", psu.rated_current_ma);
        json.number(pj, "usableCapacityPermille", psu.usable_capacity_permille);
        json.number(pj, "channelMask", psu.channel_mask);
        json.item(psu_arr, pj);
    }
    json.item(cfg_json, "powerSupplies", psu_arr);

    // Groups
    cJSON* groups_arr = json.array();
    for (uint8_t g = 0; g < 8; ++g) {
        const auto& grp = cfg.groups[g];
        cJSON* gj = json.object();
        json.number(gj, "index", g);
        json.string(gj, "name", grp.name);
        json.number(gj, "channelMask", grp.channel_mask);
        json.string(gj, "role", zone_role_name(grp.role));
        json.boolean(gj, "enabled", grp.enabled);
        json.item(groups_arr, gj);
    }
    json.item(cfg_json, "groups", groups_arr);

    return json.finish(cfg_json);
}

static cJSON* serialize_scene(const SceneConfig& sc, uint8_t index) {
    BackupJsonBuilder json;
    cJSON* s = json.object();
    json.number(s, "index", index);
    json.string(s, "name", sc.name);
    json.number(s, "globalBrightness", sc.global_brightness);
    json.number(s, "transitionMs", sc.transition_ms);

    cJSON* ch_arr = json.array();
    for (uint8_t c = 0; c < 16; ++c) {
        cJSON* c_obj = json.object();
        cJSON* l_arr = json.array();
        for (uint8_t l = 0; l < 3; ++l) {
            const auto& layer = sc.channels[c].layers[l];
            cJSON* l_obj = json.object();
            json.boolean(l_obj, "enabled", layer.enabled);
            json.number(l_obj, "effect", static_cast<int>(layer.effect));
            json.number(l_obj, "speedMs", layer.speed_ms);

            cJSON* pcol = json.object();
            json.number(pcol, "r", layer.primary_color.r);
            json.number(pcol, "g", layer.primary_color.g);
            json.number(pcol, "b", layer.primary_color.b);
            json.item(l_obj, "primaryColor", pcol);

            cJSON* scol = json.object();
            json.number(scol, "r", layer.secondary_color.r);
            json.number(scol, "g", layer.secondary_color.g);
            json.number(scol, "b", layer.secondary_color.b);
            json.item(l_obj, "secondaryColor", scol);

            json.number(l_obj, "opacity", layer.opacity);
            json.number(l_obj, "blendMode", static_cast<int>(layer.blend_mode));
            json.number(l_obj, "parameter", layer.fx_param);
            json.number(l_obj, "paletteId", layer.palette_id);
            json.item(l_arr, l_obj);
        }
        json.item(c_obj, "layers", l_arr);
        json.item(ch_arr, c_obj);
    }
    json.item(s, "channels", ch_arr);
    return json.finish(s);
}

static cJSON* serialize_scene_motion(const SceneMotionConfig& sm, uint8_t index) {
    BackupJsonBuilder json;
    cJSON* m_obj = json.object();
    json.number(m_obj, "index", index);
    json.string(m_obj, "transitionCurve", transition_curve_name(sm.transition_curve));
    json.number(m_obj, "trackCount", sm.track_count);
    cJSON* tracks = json.array();
    for (uint8_t t = 0; t < sm.track_count && t < kAutomationTracksPerScene; ++t) {
        const auto& tr = sm.tracks[t];
        cJSON* tobj = json.object();
        json.string(tobj, "target", automation_target_name(tr.target));
        json.number(tobj, "channel", tr.channel);
        json.number(tobj, "layer", tr.layer);
        json.string(tobj, "curve", automation_curve_name(tr.curve));
        json.boolean(tobj, "loop", tr.loop);
        json.number(tobj, "periodMs", tr.period_ms);
        cJSON* pts = json.array();
        for (uint8_t p = 0; p < tr.point_count && p < kAutomationPointsPerTrack; ++p) {
            cJSON* pobj = json.object();
            json.number(pobj, "position", tr.points[p].position);
            json.number(pobj, "value", tr.points[p].value);
            json.item(pts, pobj);
        }
        json.item(tobj, "points", pts);
        json.item(tracks, tobj);
    }
    json.item(m_obj, "tracks", tracks);
    return json.finish(m_obj);
}

static cJSON* serialize_timeline(const Sequence& seq) {
    BackupJsonBuilder json;
    cJSON* seq_obj = json.object();
    json.number(seq_obj, "bpm", seq.bpm);
    json.boolean(seq_obj, "enabled", seq.enabled);
    cJSON* trs = json.array();
    for (uint8_t t = 0; t < 16; ++t) {
        const auto& track = seq.tracks[t];
        cJSON* trobj = json.object();
        json.number(trobj, "channel_id", track.channel_id);
        json.boolean(trobj, "enabled", track.enabled);
        cJSON* steps = json.array();
        for (uint8_t s = 0; s < track.num_steps && s < 16; ++s) {
            cJSON* sobj = json.object();
            json.number(sobj, "preset_index", track.steps[s].preset_index);
            json.number(sobj, "length_beats", track.steps[s].length_beats);
            json.item(steps, sobj);
        }
        json.item(trobj, "steps", steps);
        json.item(trs, trobj);
    }
    json.item(seq_obj, "tracks", trs);
    return json.finish(seq_obj);
}

static cJSON* serialize_calendar(const CalendarSchedule& cal) {
    BackupJsonBuilder json;
    cJSON* cal_obj = json.object();
    cJSON* entries = json.array();
    for (uint8_t e = 0; e < 8; ++e) {
        const auto& entry = cal.entries[e];
        cJSON* eobj = json.object();
        json.number(eobj, "hour", entry.hour);
        json.number(eobj, "minute", entry.minute);
        json.number(eobj, "days_of_week", entry.days_of_week);
        json.number(eobj, "preset_index", entry.preset_index);
        json.number(eobj, "action", static_cast<int>(entry.action));
        json.boolean(eobj, "enabled", entry.enabled);
        json.number(eobj, "cue_list_index", entry.cue_list_index);
        json.item(entries, eobj);
    }
    json.item(cal_obj, "entries", entries);
    return json.finish(cal_obj);
}

static cJSON* serialize_cue_list(const CueList& cl, uint8_t index) {
    BackupJsonBuilder json;
    cJSON* cl_obj = json.object();
    json.number(cl_obj, "index", index);
    json.string(cl_obj, "name", cl.name);
    json.string(cl_obj, "mode", cue_run_mode_name(cl.mode));
    json.string(cl_obj, "endAction", cue_end_action_name(cl.end_action));
    json.number(cl_obj, "checksum", cl.checksum);
    cJSON* cues = json.array();
    for (uint8_t c = 0; c < cl.count && c < kMaxCuesPerList; ++c) {
        cJSON* cobj = json.object();
        json.number(cobj, "sceneIndex", cl.cues[c].scene_index);
        json.number(cobj, "holdMs", cl.cues[c].hold_ms);
        json.number(cobj, "fadeMs", cl.cues[c].fade_ms);
        json.string(cobj, "curve", transition_curve_name(cl.cues[c].curve));
        json.string(cobj, "label", cl.cues[c].label);
        json.item(cues, cobj);
    }
    json.item(cl_obj, "cues", cues);
    return json.finish(cl_obj);
}

// Restore must reject invalid fields, not narrow/wrap them into a new show.
static bool backup_integer_tree(const cJSON* value, unsigned depth = 0) {
    if (!value || depth > 32) return false;
    if (cJSON_IsNumber(value) &&
        (!std::isfinite(value->valuedouble) || std::trunc(value->valuedouble) != value->valuedouble)) return false;
    for (const cJSON* child = value->child; child; child = child->next)
        if (!backup_integer_tree(child, depth + 1)) return false;
    return true;
}

template <typename T>
static bool backup_number(const cJSON* obj, const char* key, T& value,
                          double minimum, double maximum) {
    if (!cJSON_GetObjectItemCaseSensitive(obj, key)) return true;
    return lg::json::get_number(obj, key, value) && value >= minimum && value <= maximum;
}

static bool backup_string(const cJSON* obj, const char* key, char* value, size_t capacity) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return true;
    return cJSON_IsString(item) && item->valuestring && std::strlen(item->valuestring) < capacity &&
           lg::json::get_string(obj, key, value, capacity);
}

static bool backup_bool(const cJSON* obj, const char* key, bool& value) {
    return !cJSON_GetObjectItemCaseSensitive(obj, key) || lg::json::get_bool(obj, key, value);
}

static bool backup_error(char* error, size_t capacity, const char* code) {
    snprintf(error, capacity, "%s", code);
    return false;
}

static bool parse_scene_record(cJSON* obj, SceneConfig& sc, char* err_buf, size_t err_size) {
    if (!cJSON_IsObject(obj) || !backup_integer_tree(obj) ||
        !backup_string(obj, "name", sc.name, sizeof(sc.name)) ||
        !backup_number(obj, "globalBrightness", sc.global_brightness, 0, 255) ||
        !backup_number(obj, "transitionMs", sc.transition_ms, 0, 60000))
        return backup_error(err_buf, err_size, "invalid_scene_fields");

    const cJSON* channels = cJSON_GetObjectItemCaseSensitive(obj, "channels");
    if (!channels) return true;
    if (!cJSON_IsArray(channels) || cJSON_GetArraySize(channels) > 16)
        return backup_error(err_buf, err_size, "invalid_scene_channels");
    unsigned channel_index = 0;
    for (const cJSON* channel = channels->child; channel; channel = channel->next, ++channel_index) {
        if (!cJSON_IsObject(channel)) return backup_error(err_buf, err_size, "invalid_scene_channel");
        const cJSON* layers = cJSON_GetObjectItemCaseSensitive(channel, "layers");
        if (!layers) continue;
        if (!cJSON_IsArray(layers) || cJSON_GetArraySize(layers) > 3)
            return backup_error(err_buf, err_size, "invalid_scene_layers");
        unsigned layer_index = 0;
        for (const cJSON* item = layers->child; item; item = item->next, ++layer_index) {
            auto& layer = sc.channels[channel_index].layers[layer_index];
            const char* error = nullptr;
            if (!parse_and_validate_layer_obj(item, layer, error))
                return backup_error(err_buf, err_size, error ? error : "invalid_scene_layer");
            // Earlier v4 exports used parameter; current API payloads use fxParam.
            if (!backup_number(item, "parameter", layer.fx_param, 0, 255))
                return backup_error(err_buf, err_size, "invalid_parameter");
            const cJSON* fx = cJSON_GetObjectItemCaseSensitive(item, "fxParam");
            if (fx && fx->valuedouble != layer.fx_param)
                return backup_error(err_buf, err_size, "conflicting_parameter");
        }
    }
    return true;
}

static bool parse_scene_motion_record(cJSON* obj, SceneMotionConfig& sm, char* err_buf, size_t err_size) {
    if (!cJSON_IsObject(obj) || !backup_integer_tree(obj))
        return backup_error(err_buf, err_size, "invalid_scene_motion");
    const char* error = nullptr;
    if (!parse_and_validate_motion_json(obj, 0, sm, error))
        return backup_error(err_buf, err_size, error ? error : "invalid_scene_motion");
    return true;
}

static bool parse_cue_list_record(cJSON* obj, CueList& cl, char* err_buf, size_t err_size) {
    if (!cJSON_IsObject(obj) || !backup_integer_tree(obj) ||
        !backup_string(obj, "name", cl.name, sizeof(cl.name)))
        return backup_error(err_buf, err_size, "invalid_cue_list");
    const cJSON* mode = cJSON_GetObjectItemCaseSensitive(obj, "mode");
    if (mode && (!cJSON_IsString(mode) || !parse_cue_run_mode(mode->valuestring, cl.mode)))
        return backup_error(err_buf, err_size, "invalid_cue_run_mode");
    const cJSON* end = cJSON_GetObjectItemCaseSensitive(obj, "endAction");
    if (end) {
        if (cJSON_IsString(end)) {
            if (!parse_cue_end_action(end->valuestring, cl.end_action))
                return backup_error(err_buf, err_size, "invalid_cue_end_action");
        } else {
            uint8_t action = 0;
            if (!lg::json::get_number(obj, "endAction", action) || action > 2)
                return backup_error(err_buf, err_size, "invalid_cue_end_action");
            cl.end_action = static_cast<CueEndAction>(action);
        }
    }
    const cJSON* cues = cJSON_GetObjectItemCaseSensitive(obj, "cues");
    if (!cues) return true;
    if (!cJSON_IsArray(cues) || cJSON_GetArraySize(cues) > kMaxCuesPerList)
        return backup_error(err_buf, err_size, "invalid_cues");
    cl.count = 0;
    for (const cJSON* item = cues->child; item; item = item->next) {
        auto& cue = cl.cues[cl.count];
        if (!cJSON_IsObject(item) || !lg::json::get_number(item, "sceneIndex", cue.scene_index) ||
            cue.scene_index >= kMaxPresets || !backup_number(item, "holdMs", cue.hold_ms, 0, 3600000) ||
            !backup_number(item, "fadeMs", cue.fade_ms, 0, 60000) ||
            !backup_string(item, "label", cue.label, sizeof(cue.label)))
            return backup_error(err_buf, err_size, "invalid_cue_fields");
        const cJSON* curve = cJSON_GetObjectItemCaseSensitive(item, "curve");
        if (curve && (!cJSON_IsString(curve) || !parse_transition_curve(curve->valuestring, cue.curve)))
            return backup_error(err_buf, err_size, "invalid_cue_curve");
        ++cl.count;
    }
    return true;
}

static bool parse_timeline_record(cJSON* obj, Sequence& seq, char* err_buf, size_t err_size) {
    if (!cJSON_IsObject(obj) || !backup_integer_tree(obj) ||
        !backup_number(obj, "bpm", seq.bpm, 10, 500) || !backup_bool(obj, "enabled", seq.enabled))
        return backup_error(err_buf, err_size, "invalid_timeline");
    // Match the canonical physical track layout used by ConfigStore and the
    // timeline API. Missing tracks are empty, not duplicate channel-zero rows.
    for (uint8_t i = 0; i < 16; ++i) {
        seq.tracks[i].channel_id = i;
        seq.tracks[i].num_steps = 0;
        seq.tracks[i].enabled = false;
    }
    const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(obj, "tracks");
    if (!tracks) return true;
    if (!cJSON_IsArray(tracks) || cJSON_GetArraySize(tracks) > 16)
        return backup_error(err_buf, err_size, "invalid_timeline_tracks");
    unsigned index = 0;
    for (const cJSON* item = tracks->child; item; item = item->next, ++index) {
        auto& track = seq.tracks[index];
        if (!cJSON_IsObject(item) || !backup_number(item, "channel_id", track.channel_id, 0, 15) ||
            track.channel_id != index || !backup_bool(item, "enabled", track.enabled))
            return backup_error(err_buf, err_size, "invalid_timeline_track");
        const cJSON* steps = cJSON_GetObjectItemCaseSensitive(item, "steps");
        if (!steps) continue;
        if (!cJSON_IsArray(steps) || cJSON_GetArraySize(steps) > 16)
            return backup_error(err_buf, err_size, "invalid_timeline_steps");
        track.num_steps = 0;
        for (const cJSON* step = steps->child; step; step = step->next) {
            auto& out = track.steps[track.num_steps];
            if (!cJSON_IsObject(step) || !backup_number(step, "preset_index", out.preset_index, 0, kMaxPresets - 1) ||
                !backup_number(step, "length_beats", out.length_beats, 1, 64))
                return backup_error(err_buf, err_size, "invalid_timeline_step");
            ++track.num_steps;
        }
    }
    return true;
}

static bool parse_calendar_record(cJSON* obj, CalendarSchedule& cal, char* err_buf, size_t err_size) {
    if (!cJSON_IsObject(obj) || !backup_integer_tree(obj))
        return backup_error(err_buf, err_size, "invalid_calendar");
    const cJSON* entries = cJSON_GetObjectItemCaseSensitive(obj, "entries");
    if (!entries) return true;
    if (!cJSON_IsArray(entries) || cJSON_GetArraySize(entries) > 8)
        return backup_error(err_buf, err_size, "invalid_calendar_entries");
    unsigned index = 0;
    for (const cJSON* item = entries->child; item; item = item->next, ++index) {
        auto& entry = cal.entries[index];
        uint8_t action = static_cast<uint8_t>(entry.action);
        if (!cJSON_IsObject(item) || !backup_number(item, "hour", entry.hour, 0, 23) ||
            !backup_number(item, "minute", entry.minute, 0, 59) ||
            !backup_number(item, "days_of_week", entry.days_of_week, 0, 127) ||
            !backup_number(item, "preset_index", entry.preset_index, 0, kMaxPresets - 1) ||
            !backup_number(item, "action", action, 0, 2) ||
            !backup_number(item, "cue_list_index", entry.cue_list_index, 0, kMaxCueLists - 1) ||
            !backup_bool(item, "enabled", entry.enabled))
            return backup_error(err_buf, err_size, "invalid_calendar_entry");
        entry.action = static_cast<CalendarAction>(action);
    }
    return true;
}

// ── Streaming Backup Export (§25, §30, §31) ──

static bool backup_record_missing(esp_err_t error) {
    return error == ESP_ERR_NOT_FOUND || error == ESP_ERR_NVS_NOT_FOUND;
}

struct BackupExportScratch {
    DeviceConfig config{};
    SceneConfig scene{};
    SceneMotionConfig motion{};
    Sequence timeline{};
    CalendarSchedule calendar{};
    CueList cue_list{};
};

esp_err_t get_backup(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;
    auto* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    auto* store = api_ptr->config();
    if (!store) return send_error(request, "500 Internal Server Error", "config_not_ready");
    auto scratch = std::unique_ptr<BackupExportScratch>(new (std::nothrow) BackupExportScratch{});
    if (!scratch) {
        note_api_allocation_failure();
        return send_error(request, "500 Internal Server Error", "out_of_memory");
    }
    if (store->load_config(scratch->config, false) != ESP_OK)
        return send_error(request, "500 Internal Server Error", "backup_config_load_failed");

    const auto& identity = api_ptr->identity();
    char filename[128];
    snprintf(filename, sizeof(filename), "attachment; filename=\"lumigrid-%s-backup.lgbak\"", identity.id);
    httpd_resp_set_type(request, "application/x-lumigrid-backup");
    httpd_resp_set_hdr(request, "Content-Disposition", filename);
    uint32_t total_crc = 0, record_count = 0;
    auto send_record = [&](cJSON* object) -> esp_err_t {
        if (!object) { note_api_allocation_failure(); return ESP_ERR_NO_MEM; }
        char* text = cJSON_PrintUnformatted(object);
        cJSON_Delete(object);
        if (!text) { note_api_allocation_failure(); return ESP_ERR_NO_MEM; }
        const size_t length = std::strlen(text);
        esp_err_t error = httpd_resp_send_chunk(request, text, length);
        if (error == ESP_OK) total_crc = esp_rom_crc32_le(total_crc, reinterpret_cast<const uint8_t*>(text), length);
        cJSON_free(text);
        if (error != ESP_OK) return error;
        error = httpd_resp_send_chunk(request, "\n", 1);
        if (error == ESP_OK) {
            total_crc = esp_rom_crc32_le(total_crc, reinterpret_cast<const uint8_t*>("\n"), 1);
            ++record_count;
        }
        return error;
    };
    auto send_data = [&](const char* type, cJSON* data, int index = -1) -> esp_err_t {
        BackupJsonBuilder json;
        cJSON* record = json.object();
        json.string(record, "type", type);
        if (index >= 0) json.number(record, "index", index);
        json.item(record, "data", data);
        return send_record(json.finish(record));
    };
    {
        BackupJsonBuilder json;
        cJSON* header = json.object();
        json.string(header, "type", "header");
        json.number(header, "schema", 4);
        cJSON* source = json.object();
        json.string(source, "nodeId", identity.id);
        json.string(source, "family", "led-node");
        json.string(source, "model", identity.model);
        json.string(source, "firmwareVersion", identity.fw_version);
        json.string(source, "hardwareRevision", identity.hw_revision);
        json.number(source, "partitionLayoutVersion", kPartitionLayoutVersion);
        json.number(source, "configuredFlashBytes", capture_storage().configured_bytes);
        json.item(header, "source", source);
        cJSON* policy = json.object();
        json.boolean(policy, "authIncluded", false);
        json.boolean(policy, "networkSecretsIncluded", false);
        json.boolean(policy, "syncSecretIncluded", false);
        json.item(header, "policy", policy);
        if (send_record(json.finish(header)) != ESP_OK) return ESP_FAIL;
    }
    if (send_data("deviceConfig", serialize_device_config(scratch->config)) != ESP_OK) return ESP_FAIL;
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        const esp_err_t error = store->load_scene(i, scratch->scene, false);
        if (error == ESP_OK) {
            if (send_data("scene", serialize_scene(scratch->scene, i), i) != ESP_OK) return ESP_FAIL;
        } else if (!backup_record_missing(error)) return error;
    }
    for (uint8_t i = 0; i < kMaxPresets; ++i) {
        const esp_err_t error = store->load_scene_motion(i, scratch->motion);
        if (error == ESP_OK) {
            // Include empty records too: restoring them must clear old motion.
            if (send_data("sceneMotion", serialize_scene_motion(scratch->motion, i), i) != ESP_OK) return ESP_FAIL;
        } else if (!backup_record_missing(error)) return error;
    }
    esp_err_t error = store->load_sequence(scratch->timeline, nullptr, false);
    if (error == ESP_OK) {
        if (send_data("timeline", serialize_timeline(scratch->timeline)) != ESP_OK) return ESP_FAIL;
    } else if (!backup_record_missing(error)) return error;
    error = store->load_calendar_schedule(scratch->calendar);
    if (error == ESP_OK) {
        if (send_data("calendar", serialize_calendar(scratch->calendar)) != ESP_OK) return ESP_FAIL;
    } else if (!backup_record_missing(error)) return error;
    for (uint8_t i = 0; i < kMaxCueLists; ++i) {
        error = store->load_cue_list(i, scratch->cue_list);
        if (error == ESP_OK) {
            if (send_data("cueList", serialize_cue_list(scratch->cue_list, i), i) != ESP_OK) return ESP_FAIL;
        } else if (!backup_record_missing(error)) return error;
    }
    {
        BackupJsonBuilder json;
        cJSON* end = json.object();
        json.string(end, "type", "end");
        json.number(end, "records", record_count + 1);
        json.number(end, "checksum", total_crc);
        if (send_record(json.finish(end)) != ESP_OK) return ESP_FAIL;
    }
    api::AuditLog::instance().log(api::ApiRole::Admin, "backup_export", "Recovery package v4 exported");
    return httpd_resp_send_chunk(request, nullptr, 0);
}

// ── Restore Parsed Package Model & Ingest ──

struct ParsedBackupPackage {
    uint8_t schema = 4;
    char node_id[32] = "";
    char family[32] = "led-node";
    char model[32] = "lg-led-v1";
    char firmware_version[32] = "";
    uint32_t checksum = 0;

    bool has_device_config = false;
    DeviceConfig device_config{};

    bool has_scenes[lg::kMaxPresets] = {};
    SceneConfig scenes[lg::kMaxPresets] = {};

    bool has_motions[lg::kMaxPresets] = {};
    SceneMotionConfig motions[lg::kMaxPresets] = {};

    bool has_timeline = false;
    Sequence timeline{};

    bool has_calendar = false;
    CalendarSchedule calendar{};

    bool has_cue_lists[lg::kMaxCueLists] = {};
    CueList cue_lists[lg::kMaxCueLists] = {};

    uint32_t scene_count = 0;
    uint32_t motion_count = 0;
    uint32_t cue_list_count = 0;

    char error[128] = "";
};

// LG-038.3 §49: restore stays record-bounded. Individual record limit.
static constexpr size_t kMaxRestoreRecordSize = 16384;

static esp_err_t ingest_backup_package(httpd_req_t* request, ParsedBackupPackage& pkg, ConfigStore* store) {
    auto fail = [&](const char* code, esp_err_t error = ESP_ERR_INVALID_ARG) -> esp_err_t {
        snprintf(pkg.error, sizeof(pkg.error), "%s", code);
        return error;
    };
    if (request->content_len <= 0 || request->content_len > 1024 * 1024)
        return fail("invalid_payload_size", ESP_ERR_INVALID_SIZE);
    std::unique_ptr<char[]> line(new (std::nothrow) char[kMaxRestoreRecordSize + 1]);
    if (!line) { note_api_allocation_failure(); return fail("out_of_memory", ESP_ERR_NO_MEM); }
    using JsonOwner = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    size_t line_size = 0;
    unsigned timeouts = 0;
    bool first_line = true, legacy = false, header_seen = false, end_seen = false;
    uint32_t crc = 0, record_count = 0;
    std::string legacy_payload;

    auto parse_data_record = [&](const char* type, cJSON* record) -> esp_err_t {
        cJSON* data = cJSON_GetObjectItemCaseSensitive(record, "data");
        if (!cJSON_IsObject(data)) return fail("missing_record_data");
        if (std::strcmp(type, "deviceConfig") == 0) {
            if (pkg.has_device_config) return fail("duplicate_device_config");
            // Preserve omitted values and board identity from the target node.
            // This object is heap-owned as part of pkg, not another large local.
            if (store) {
                const esp_err_t error = store->load_config(pkg.device_config, false);
                if (error != ESP_OK) return fail("current_config_load_failed", error);
            }
            if (!parse_and_validate_device_config(data, pkg.device_config, pkg.device_config,
                                                   pkg.error, sizeof(pkg.error))) return ESP_ERR_INVALID_ARG;
            ConfigStore::ConfigValidationResult validation{};
            const esp_err_t validated = ConfigStore::validate_config(pkg.device_config, &validation);
            if (validated != ESP_OK) return fail(validation.first_error[0] ? validation.first_error : "invalid_device_config", validated);
            pkg.has_device_config = true;
        } else if (std::strcmp(type, "scene") == 0 || std::strcmp(type, "sceneMotion") == 0) {
            uint8_t index = 0;
            if (!lg::json::get_number(record, "index", index) || index >= kMaxPresets)
                return fail("invalid_scene_index");
            if (std::strcmp(type, "scene") == 0) {
                if (pkg.has_scenes[index]) return fail("duplicate_scene");
                if (!parse_scene_record(data, pkg.scenes[index], pkg.error, sizeof(pkg.error))) return ESP_ERR_INVALID_ARG;
                pkg.has_scenes[index] = true; ++pkg.scene_count;
            } else {
                if (pkg.has_motions[index]) return fail("duplicate_scene_motion");
                if (!parse_scene_motion_record(data, pkg.motions[index], pkg.error, sizeof(pkg.error))) return ESP_ERR_INVALID_ARG;
                pkg.has_motions[index] = true; ++pkg.motion_count;
            }
        } else if (std::strcmp(type, "cueList") == 0) {
            uint8_t index = 0;
            if (!lg::json::get_number(record, "index", index) || index >= kMaxCueLists)
                return fail("invalid_cue_list_index");
            if (pkg.has_cue_lists[index]) return fail("duplicate_cue_list");
            if (!parse_cue_list_record(data, pkg.cue_lists[index], pkg.error, sizeof(pkg.error))) return ESP_ERR_INVALID_ARG;
            pkg.has_cue_lists[index] = true; ++pkg.cue_list_count;
        } else if (std::strcmp(type, "timeline") == 0) {
            if (pkg.has_timeline) return fail("duplicate_timeline");
            if (!parse_timeline_record(data, pkg.timeline, pkg.error, sizeof(pkg.error))) return ESP_ERR_INVALID_ARG;
            pkg.has_timeline = true;
        } else if (std::strcmp(type, "calendar") == 0) {
            if (pkg.has_calendar) return fail("duplicate_calendar");
            if (!parse_calendar_record(data, pkg.calendar, pkg.error, sizeof(pkg.error))) return ESP_ERR_INVALID_ARG;
            pkg.has_calendar = true;
        } else return fail("unknown_record_type");
        return ESP_OK;
    };

    int remaining = request->content_len;
    while (remaining > 0) {
        char ch = 0;
        const int received = httpd_req_recv(request, &ch, 1);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts >= 3) return fail("receive_timeout", ESP_ERR_TIMEOUT);
            continue;
        }
        if (received != 1) return fail("receive_error", ESP_FAIL);
        --remaining;
        if (ch != '\n') {
            if (line_size == kMaxRestoreRecordSize) return fail("record_too_large", ESP_ERR_INVALID_SIZE);
            line[line_size++] = ch;
        }
        if (ch != '\n' && remaining != 0) continue;
        line[line_size] = '\0';
        JsonOwner record(nullptr, &cJSON_Delete);
        if (first_line) {
            first_line = false;
            record.reset(parse_admitted_json(line.get(), line_size));
            // A v4 header is a complete first line. Legacy v3 can be pretty
            // printed, so retain a bounded payload and validate its version.
            legacy = !record || !cJSON_GetObjectItemCaseSensitive(record.get(), "type");
        }
        if (legacy) {
            const size_t appended = line_size + (ch == '\n' ? 1u : 0u);
            if (appended > 32768u - legacy_payload.size()) return fail("legacy_v3_payload_too_large", ESP_ERR_INVALID_SIZE);
            legacy_payload.append(line.get(), line_size);
            if (ch == '\n') legacy_payload.push_back('\n');
            line_size = 0;
            continue;
        }
        if (end_seen) return fail("record_after_end");
        if (!record) record.reset(parse_admitted_json(line.get(), line_size));
        if (!record) return fail("malformed_record");
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(record.get(), "type");
        if (!cJSON_IsString(type) || !type->valuestring) return fail("missing_record_type");
        if (std::strcmp(type->valuestring, "header") == 0) {
            uint8_t schema = 0;
            if (header_seen || record_count != 0) return fail("duplicate_header");
            if (!lg::json::get_number(record.get(), "schema", schema) || schema != 4) return fail("unsupported_backup_schema");
            const cJSON* source = cJSON_GetObjectItemCaseSensitive(record.get(), "source");
            if (!cJSON_IsObject(source) || !lg::json::get_string(source, "family", pkg.family, sizeof(pkg.family)) ||
                std::strcmp(pkg.family, "led-node") != 0) return fail("backup_family_mismatch");
            if (!backup_string(source, "nodeId", pkg.node_id, sizeof(pkg.node_id)) ||
                !backup_string(source, "model", pkg.model, sizeof(pkg.model)) ||
                !backup_string(source, "firmwareVersion", pkg.firmware_version, sizeof(pkg.firmware_version)))
                return fail("invalid_backup_identity");
            pkg.schema = schema; header_seen = true;
        } else if (!header_seen) return fail("missing_header");
        else if (std::strcmp(type->valuestring, "end") == 0) {
            uint32_t expected_count = 0, expected_crc = 0;
            if (!lg::json::get_number(record.get(), "records", expected_count) || expected_count != record_count + 1)
                return fail("backup_record_count_mismatch");
            if (!lg::json::get_number(record.get(), "checksum", expected_crc) || expected_crc != crc)
                return fail("backup_checksum_mismatch");
            pkg.checksum = crc; end_seen = true;
        } else {
            const esp_err_t error = parse_data_record(type->valuestring, record.get());
            if (error != ESP_OK) return error;
        }
        if (!end_seen) {
            crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(line.get()), line_size);
            if (ch == '\n') crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>("\n"), 1);
            ++record_count;
        }
        line_size = 0;
    }
    if (legacy) {
        JsonOwner root(parse_admitted_json(legacy_payload.data(), legacy_payload.size()), &cJSON_Delete);
        uint8_t version = 0;
        if (!root || !lg::json::get_number(root.get(), "version", version) || version != 3)
            return fail("unsupported_legacy_backup");
        pkg.schema = 3;
        pkg.checksum = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(legacy_payload.data()), legacy_payload.size());
        // Wrap borrowed legacy data only for the common validation path. The
        // wrapper owns reference nodes, never the root's source objects.
        auto legacy_record = [&](const char* type, cJSON* data, int index) -> esp_err_t {
            JsonOwner wrapper(cJSON_CreateObject(), &cJSON_Delete);
            if (!wrapper) return fail("out_of_memory", ESP_ERR_NO_MEM);
            if (index >= 0 && !cJSON_AddNumberToObject(wrapper.get(), "index", index)) return fail("out_of_memory", ESP_ERR_NO_MEM);
            if (!cJSON_AddItemReferenceToObject(wrapper.get(), "data", data)) return fail("out_of_memory", ESP_ERR_NO_MEM);
            return parse_data_record(type, wrapper.get());
        };
        cJSON* config = cJSON_GetObjectItemCaseSensitive(root.get(), "config");
        if (config) {
            const esp_err_t error = legacy_record("deviceConfig", config, -1);
            if (error != ESP_OK) return error;
        }
        struct LegacyArray { const char* key; const char* type; };
        for (const LegacyArray& source : {LegacyArray{"presets", "scene"}, {"scene_motion", "sceneMotion"}, {"cue_lists", "cueList"}}) {
            cJSON* array = cJSON_GetObjectItemCaseSensitive(root.get(), source.key);
            if (!array) continue;
            if (!cJSON_IsArray(array)) return fail("invalid_legacy_record_array");
            for (cJSON* item = array->child; item; item = item->next) {
                uint8_t index = 0;
                if (!cJSON_IsObject(item) || !lg::json::get_number(item, "index", index)) return fail("invalid_legacy_index");
                const esp_err_t error = legacy_record(source.type, item, index);
                if (error != ESP_OK) return error;
            }
        }
    } else if (!header_seen || !end_seen) return fail("incomplete_backup");
    if (!pkg.has_device_config && !pkg.scene_count && !pkg.motion_count &&
        !pkg.cue_list_count && !pkg.has_timeline && !pkg.has_calendar) return fail("empty_backup");

    // Product family check (§37)
    if (strlen(pkg.family) > 0 && strcmp(pkg.family, "led-node") != 0) {
        snprintf(pkg.error, sizeof(pkg.error), "backup_family_mismatch");
        return ESP_ERR_INVALID_ARG;
    }

    // Cross-reference validations (§36)
    auto scene_exists_in_package_or_store = [&](uint8_t idx) -> bool {
        if (idx >= lg::kMaxPresets) return false;
        if (pkg.has_scenes[idx]) return true;
        return store && store->scene_exists(idx);
    };

    auto cue_list_exists_in_package_or_store = [&](uint8_t idx) -> bool {
        if (idx >= lg::kMaxCueLists) return false;
        if (pkg.has_cue_lists[idx]) return true;
        if (!store) return false;
        CueList cl;
        return store->load_cue_list(idx, cl) == ESP_OK && cl.count > 0;
    };

    // Timeline cross-ref
    if (pkg.has_timeline) {
        for (uint8_t t = 0; t < 16; ++t) {
            const auto& tr = pkg.timeline.tracks[t];
            if (tr.enabled) {
                for (uint8_t s = 0; s < tr.num_steps && s < 16; ++s) {
                    if (!scene_exists_in_package_or_store(tr.steps[s].preset_index)) {
                        snprintf(pkg.error, sizeof(pkg.error), "timeline_references_missing_scene");
                        return ESP_ERR_INVALID_ARG;
                    }
                }
            }
        }
    }

    // Calendar cross-ref
    if (pkg.has_calendar) {
        for (uint8_t e = 0; e < 8; ++e) {
            const auto& ent = pkg.calendar.entries[e];
            if (ent.enabled) {
                if (ent.action == CalendarAction::Preset) {
                    if (!scene_exists_in_package_or_store(ent.preset_index)) {
                        snprintf(pkg.error, sizeof(pkg.error), "calendar_references_missing_scene");
                        return ESP_ERR_INVALID_ARG;
                    }
                } else if (ent.action == CalendarAction::CueList) {
                    if (!cue_list_exists_in_package_or_store(ent.cue_list_index)) {
                        snprintf(pkg.error, sizeof(pkg.error), "calendar_references_missing_cue_list");
                        return ESP_ERR_INVALID_ARG;
                    }
                }
            }
        }
    }

    // Cue list cross-ref
    for (uint8_t i = 0; i < lg::kMaxCueLists; ++i) {
        if (pkg.has_cue_lists[i]) {
            for (uint8_t c = 0; c < pkg.cue_lists[i].count; ++c) {
                if (!scene_exists_in_package_or_store(pkg.cue_lists[i].cues[c].scene_index)) {
                    snprintf(pkg.error, sizeof(pkg.error), "cue_references_missing_scene");
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }

    return ESP_OK;
}

// ── Restore Preflight Validation (§34, §35) ──

esp_err_t post_restore_validate(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->config()) {
        return send_error(request, "500 Internal Server Error", "ConfigStore not available");
    }

    constexpr uint32_t scratch = sizeof(ParsedBackupPackage) + kMaxRestoreRecordSize + 32768u;
    if (!check_memory_admission_snapshot(capture_runtime_memory(), scratch,
            kPostTopologyHeapReserveBytes, sizeof(ParsedBackupPackage)).accepted)
        return send_memory_admission_error(request, scratch);
    std::unique_ptr<ParsedBackupPackage> storage(new (std::nothrow) ParsedBackupPackage{});
    if (!storage) { note_api_allocation_failure(); return send_memory_admission_error(request, scratch); }
    auto& pkg = *storage;
    esp_err_t err = ingest_backup_package(request, pkg, api_ptr->config());
    if (err != ESP_OK) {
        cJSON* err_resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(err_resp, "valid", false);
        cJSON_AddStringToObject(err_resp, "error", pkg.error);
        return send_error_json(request, "400 Bad Request", err_resp);
    }

    api::AuditLog::instance().log(api::ApiRole::Admin, "restore_validate", "Restore package preflight validated");

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "valid", true);
    cJSON_AddNumberToObject(resp, "schema", pkg.schema);

    cJSON* src = cJSON_CreateObject();
    cJSON_AddStringToObject(src, "nodeId", pkg.node_id);
    cJSON_AddStringToObject(src, "firmwareVersion", pkg.firmware_version);
    cJSON_AddStringToObject(src, "family", pkg.family);
    cJSON_AddStringToObject(src, "model", pkg.model);
    cJSON_AddItemToObject(resp, "source", src);

    cJSON_AddNumberToObject(resp, "packageChecksum", pkg.checksum);

    cJSON* plan = cJSON_CreateObject();
    cJSON_AddBoolToObject(plan, "deviceConfig", pkg.has_device_config);
    cJSON_AddNumberToObject(plan, "scenes", pkg.scene_count);
    cJSON_AddNumberToObject(plan, "sceneMotions", pkg.motion_count);
    cJSON_AddBoolToObject(plan, "timeline", pkg.has_timeline);
    cJSON_AddBoolToObject(plan, "calendar", pkg.has_calendar);
    cJSON_AddNumberToObject(plan, "cueLists", pkg.cue_list_count);
    cJSON_AddItemToObject(resp, "plan", plan);

    cJSON* pres = cJSON_CreateObject();
    cJSON_AddBoolToObject(pres, "authentication", true);
    cJSON_AddBoolToObject(pres, "networkCredentials", true);
    cJSON_AddBoolToObject(pres, "nodeIdentity", true);
    cJSON_AddBoolToObject(pres, "syncToken", true);
    cJSON_AddItemToObject(resp, "preserved", pres);

    cJSON* warns = cJSON_CreateArray();
    cJSON_AddItemToObject(resp, "warnings", warns);

    return send_json(request, resp);
}

// ── Transactional Restore with In-Memory Rollback Snapshot (§38-§45) ──

struct RestoreRollbackSnapshot {
    DeviceConfig old_cfg{};
    bool has_scenes[lg::kMaxPresets] = {};
    SceneConfig old_scenes[lg::kMaxPresets] = {};
    bool has_motions[lg::kMaxPresets] = {};
    SceneMotionConfig old_motions[lg::kMaxPresets] = {};
    bool has_cue_lists[lg::kMaxCueLists] = {};
    CueList old_cue_lists[lg::kMaxCueLists] = {};
    bool has_timeline = false;
    Sequence old_timeline{};
    bool has_calendar = false;
    CalendarSchedule old_calendar{};
    bool attempted_scenes[lg::kMaxPresets] = {};
    bool attempted_motions[lg::kMaxPresets] = {};
    bool attempted_cue_lists[lg::kMaxCueLists] = {};
    bool attempted_timeline = false;
    bool attempted_calendar = false;
    bool attempted_config = false;
};

struct RestoreVerifyScratch {
    DeviceConfig config{};
    SceneConfig scene{};
    SceneMotionConfig motion{};
    CueList cue_list{};
    Sequence timeline{};
    CalendarSchedule calendar{};
};

template<typename T>
static bool restore_payload_matches(const T& expected, const T& actual) {
    // ConfigStore regenerates the final checksum on save. Compare the complete
    // preceding persisted payload so a valid but stale record is not accepted.
    return std::memcmp(&expected, &actual, offsetof(T, checksum)) == 0;
}

esp_err_t post_restore(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    ConfigStore* store = api_ptr->config();
    if (!store) return send_error(request, "500 Internal Server Error", "ConfigStore not available");
    if (api_ptr->current_maintenance_mode() != MaintenanceMode::None)
        return send_error(request, "409 Conflict", "maintenance_job_active");

    constexpr size_t scratch_bytes = sizeof(ParsedBackupPackage) + sizeof(RestoreRollbackSnapshot) +
                                     sizeof(RestoreVerifyScratch) + kMaxRestoreRecordSize + 32768u;
    const MemoryAdmissionResult admission = check_memory_admission_snapshot(
        capture_runtime_memory(), scratch_bytes, kPostTopologyHeapReserveBytes,
        std::max(sizeof(ParsedBackupPackage), sizeof(RestoreRollbackSnapshot)));
    if (!admission.accepted) {
        char err_body[192];
        snprintf(err_body, sizeof(err_body),
                 "{\"error\":\"insufficient_runtime_memory\",\"requiredBytes\":%u,\"freeBytes\":%u,\"reserveBytes\":%u}",
                 (unsigned)admission.required_bytes, (unsigned)capture_runtime_memory().free_8bit,
                 (unsigned)admission.reserve_bytes);
        httpd_resp_set_status(request, "507 Insufficient Storage");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, err_body);
    }

    const uint32_t cmd_id = api_ptr->command_journal().accept(CommandDomain::Restore);
    uint32_t job_id = 0;
    const esp_err_t job_error = api_ptr->job_manager().start_job(JobType::Restore, &job_id);
    if (job_error != ESP_OK) {
        api_ptr->command_journal().failed(cmd_id, job_error);
        return send_error(request, "409 Conflict", "job_already_running");
    }
    auto fail = [&](esp_err_t error, const char* detail, const char* status = "500 Internal Server Error") {
        api_ptr->job_manager().fail_job(job_id, error, detail);
        api_ptr->command_journal().failed(cmd_id, error);
        api::AuditLog::instance().log(api::ApiRole::Admin, "restore_failed", detail);
        return send_error(request, status, detail);
    };
    api_ptr->job_manager().update_progress(job_id, JobState::Preparing, 5);

    std::unique_ptr<ParsedBackupPackage> package_storage(new (std::nothrow) ParsedBackupPackage{});
    if (!package_storage) {
        note_api_allocation_failure();
        return fail(ESP_ERR_NO_MEM, "out_of_memory");
    }
    auto& pkg = *package_storage;
    esp_err_t error = ingest_backup_package(request, pkg, store);
    if (error != ESP_OK) return fail(error, pkg.error, "400 Bad Request");

    // A partial package cannot establish that an earlier interrupted restore
    // has been repaired. Keep that marker for explicit recovery/recommissioning.
    bool marker_active = true;
    error = store->read_restore_marker(marker_active);
    if (error != ESP_OK) return fail(error, "restore_marker_read_failed");
    if (marker_active) return fail(ESP_ERR_INVALID_STATE, "restore_recovery_required", "409 Conflict");

    std::unique_ptr<RestoreRollbackSnapshot> snapshot(new (std::nothrow) RestoreRollbackSnapshot{});
    std::unique_ptr<RestoreVerifyScratch> verify(new (std::nothrow) RestoreVerifyScratch{});
    if (!snapshot || !verify) {
        note_api_allocation_failure();
        return fail(ESP_ERR_NO_MEM, "out_of_memory_for_snapshot");
    }

    // Read only affected records, and distinguish absence from read/corruption
    // errors. Snapshot reads must not persist migrations or create defaults.
    if (pkg.has_device_config) {
        error = store->load_config(snapshot->old_cfg, false);
        if (error != ESP_OK) return fail(error, "snapshot_config_failed");
        pkg.device_config.sync_token = snapshot->old_cfg.sync_token;
        std::memcpy(pkg.device_config.serial_number, snapshot->old_cfg.serial_number, sizeof(pkg.device_config.serial_number));
        std::memcpy(pkg.device_config.hardware_revision, snapshot->old_cfg.hardware_revision, sizeof(pkg.device_config.hardware_revision));
        std::memcpy(pkg.device_config.admin_token, snapshot->old_cfg.admin_token, sizeof(pkg.device_config.admin_token));
        std::memcpy(pkg.device_config.installer_token, snapshot->old_cfg.installer_token, sizeof(pkg.device_config.installer_token));
        std::memcpy(pkg.device_config.operator_token, snapshot->old_cfg.operator_token, sizeof(pkg.device_config.operator_token));
        error = ConfigStore::validate_config(pkg.device_config);
        if (error != ESP_OK) return fail(error, "invalid_restored_config", "400 Bad Request");
    }
    for (uint8_t i = 0; i < lg::kMaxPresets; ++i) {
        if (pkg.has_scenes[i]) {
            error = store->load_scene(i, snapshot->old_scenes[i], false);
            if (error != ESP_OK && !backup_record_missing(error)) return fail(error, "snapshot_scene_failed");
            snapshot->has_scenes[i] = error == ESP_OK;
        }
        // Deleting a newly created scene during rollback also deletes its
        // motion. Preserve a pre-existing orphan motion even if absent in pkg.
        if (pkg.has_motions[i] || (pkg.has_scenes[i] && !snapshot->has_scenes[i])) {
            error = store->load_scene_motion(i, snapshot->old_motions[i]);
            if (error != ESP_OK && !backup_record_missing(error)) return fail(error, "snapshot_motion_failed");
            snapshot->has_motions[i] = error == ESP_OK;
        }
    }
    for (uint8_t i = 0; i < lg::kMaxCueLists; ++i) {
        if (!pkg.has_cue_lists[i]) continue;
        error = store->load_cue_list(i, snapshot->old_cue_lists[i]);
        if (error != ESP_OK && !backup_record_missing(error)) return fail(error, "snapshot_cue_list_failed");
        snapshot->has_cue_lists[i] = error == ESP_OK;
    }
    if (pkg.has_timeline) {
        error = store->load_sequence(snapshot->old_timeline, &snapshot->has_timeline, false);
        if (error != ESP_OK) return fail(error, "snapshot_timeline_failed");
        pkg.timeline.enabled = false; // ConfigStore's persisted compatibility value.
    }
    if (pkg.has_calendar) {
        error = store->load_calendar_schedule(snapshot->old_calendar, &snapshot->has_calendar);
        if (error != ESP_OK) return fail(error, "snapshot_calendar_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Preparing, 15);

    error = store->set_restore_in_progress(true, pkg.checksum);
    if (error != ESP_OK) return fail(error, "restore_marker_set_failed");
    uint32_t marked_checksum = 0;
    error = store->read_restore_marker(marker_active, &marked_checksum);
    if (error != ESP_OK || !marker_active || marked_checksum != pkg.checksum)
        return fail(error != ESP_OK ? error : ESP_ERR_INVALID_STATE, "restore_marker_verify_failed");

    auto save_scene_checked = [&](uint8_t index, const SceneConfig& value) -> esp_err_t {
        esp_err_t e = store->save_scene(index, value);
        if (e == ESP_OK) e = store->load_scene(index, verify->scene, false);
        return e == ESP_OK && !restore_payload_matches(value, verify->scene) ? ESP_ERR_INVALID_STATE : e;
    };
    auto save_motion_checked = [&](uint8_t index, const SceneMotionConfig& value) -> esp_err_t {
        esp_err_t e = store->save_scene_motion(index, value);
        if (e == ESP_OK) e = store->load_scene_motion(index, verify->motion);
        return e == ESP_OK && !restore_payload_matches(value, verify->motion) ? ESP_ERR_INVALID_STATE : e;
    };
    auto save_cue_checked = [&](uint8_t index, const CueList& value) -> esp_err_t {
        esp_err_t e = store->save_cue_list(index, value);
        if (e == ESP_OK) e = store->load_cue_list(index, verify->cue_list);
        return e == ESP_OK && !restore_payload_matches(value, verify->cue_list) ? ESP_ERR_INVALID_STATE : e;
    };
    auto save_timeline_checked = [&](const Sequence& value) -> esp_err_t {
        esp_err_t e = store->save_sequence(value);
        bool present = false;
        if (e == ESP_OK) e = store->load_sequence(verify->timeline, &present, false);
        return e == ESP_OK && (!present || !restore_payload_matches(value, verify->timeline)) ? ESP_ERR_INVALID_STATE : e;
    };
    auto save_calendar_checked = [&](const CalendarSchedule& value) -> esp_err_t {
        esp_err_t e = store->save_calendar_schedule(value);
        bool present = false;
        if (e == ESP_OK) e = store->load_calendar_schedule(verify->calendar, &present);
        return e == ESP_OK && (!present || !restore_payload_matches(value, verify->calendar)) ? ESP_ERR_INVALID_STATE : e;
    };
    auto save_config_checked = [&](const DeviceConfig& value) -> esp_err_t {
        esp_err_t e = store->save_config(value);
        if (e == ESP_OK) e = store->load_config(verify->config, false);
        return e == ESP_OK && !restore_payload_matches(value, verify->config) ? ESP_ERR_INVALID_STATE : e;
    };
    auto retain_marker = [&]() -> esp_err_t {
        bool active = true;
        uint32_t checksum = 0;
        esp_err_t e = store->read_restore_marker(active, &checksum);
        if (e == ESP_OK && active && checksum == pkg.checksum) return ESP_OK;
        e = store->set_restore_in_progress(true, pkg.checksum);
        if (e != ESP_OK) return e;
        e = store->read_restore_marker(active, &checksum);
        return e == ESP_OK && (!active || checksum != pkg.checksum) ? ESP_ERR_INVALID_STATE : e;
    };
    auto clear_marker_checked = [&]() -> esp_err_t {
        esp_err_t e = store->set_restore_in_progress(false);
        if (e == ESP_OK) {
            bool active = true;
            e = store->read_restore_marker(active);
            if (e == ESP_OK && active) e = ESP_ERR_INVALID_STATE;
        }
        if (e != ESP_OK && retain_marker() != ESP_OK)
            ESP_LOGE("restore", "Failed to re-arm restore marker after clear failure");
        return e;
    };
    auto rollback = [&]() -> esp_err_t {
        esp_err_t first_error = ESP_OK;
        auto remember = [&](esp_err_t e) { if (first_error == ESP_OK && e != ESP_OK) first_error = e; };
        auto expect_missing = [](esp_err_t e) -> esp_err_t {
            return backup_record_missing(e) ? ESP_OK : (e == ESP_OK ? ESP_ERR_INVALID_STATE : e);
        };
        for (uint8_t i = 0; i < lg::kMaxPresets; ++i) {
            if (snapshot->attempted_scenes[i]) {
                if (snapshot->has_scenes[i]) remember(save_scene_checked(i, snapshot->old_scenes[i]));
                else {
                    // Even a failing delete can have removed motion already.
                    snapshot->attempted_motions[i] = true;
                    remember(store->delete_scene(i));
                    remember(expect_missing(store->load_scene(i, verify->scene, false)));
                }
            }
            if (snapshot->attempted_motions[i]) {
                if (snapshot->has_motions[i]) remember(save_motion_checked(i, snapshot->old_motions[i]));
                else {
                    remember(store->delete_scene_motion(i));
                    remember(expect_missing(store->load_scene_motion(i, verify->motion)));
                }
            }
        }
        for (uint8_t i = 0; i < lg::kMaxCueLists; ++i) {
            if (!snapshot->attempted_cue_lists[i]) continue;
            if (snapshot->has_cue_lists[i]) remember(save_cue_checked(i, snapshot->old_cue_lists[i]));
            else {
                remember(store->delete_cue_list(i));
                remember(expect_missing(store->load_cue_list(i, verify->cue_list)));
            }
        }
        if (snapshot->attempted_timeline) {
            if (snapshot->has_timeline) remember(save_timeline_checked(snapshot->old_timeline));
            else {
                remember(store->delete_sequence());
                bool present = true;
                const esp_err_t e = store->load_sequence(verify->timeline, &present, false);
                remember(e == ESP_OK && present ? ESP_ERR_INVALID_STATE : e);
            }
        }
        if (snapshot->attempted_calendar) {
            if (snapshot->has_calendar) remember(save_calendar_checked(snapshot->old_calendar));
            else {
                remember(store->delete_calendar_schedule());
                bool present = true;
                const esp_err_t e = store->load_calendar_schedule(verify->calendar, &present);
                remember(e == ESP_OK && present ? ESP_ERR_INVALID_STATE : e);
            }
        }
        if (snapshot->attempted_config) remember(save_config_checked(snapshot->old_cfg));
        return first_error;
    };
    auto fail_after_write = [&](esp_err_t operation_error, const char* detail) {
        ESP_LOGE("restore", "Restore failed (%s, %d); rolling back attempted records", detail, (int)operation_error);
        const esp_err_t rollback_error = rollback();
        if (rollback_error != ESP_OK) {
            if (retain_marker() != ESP_OK) ESP_LOGE("restore", "Restore marker could not be confirmed active");
            return fail(rollback_error, "restore_rollback_incomplete");
        }
        const esp_err_t marker_error = clear_marker_checked();
        if (marker_error != ESP_OK) return fail(marker_error, "restore_marker_clear_failed");
        return fail(operation_error, detail);
    };

    // Mark an attempt before the call: a failing save may have committed its
    // primary slot already. Rollback never rewrites unrelated records.
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 20);
    for (uint8_t i = 0; i < lg::kMaxPresets; ++i) {
        if (!pkg.has_scenes[i]) continue;
        snapshot->attempted_scenes[i] = true;
        error = save_scene_checked(i, pkg.scenes[i]);
        if (error != ESP_OK) return fail_after_write(error, "save_or_verify_scene_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 55);
    for (uint8_t i = 0; i < lg::kMaxPresets; ++i) {
        if (!pkg.has_motions[i]) continue;
        snapshot->attempted_motions[i] = true;
        error = save_motion_checked(i, pkg.motions[i]);
        if (error != ESP_OK) return fail_after_write(error, "save_or_verify_motion_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 65);
    for (uint8_t i = 0; i < lg::kMaxCueLists; ++i) {
        if (!pkg.has_cue_lists[i]) continue;
        snapshot->attempted_cue_lists[i] = true;
        error = save_cue_checked(i, pkg.cue_lists[i]);
        if (error != ESP_OK) return fail_after_write(error, "save_or_verify_cue_list_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 75);
    if (pkg.has_timeline) {
        snapshot->attempted_timeline = true;
        error = save_timeline_checked(pkg.timeline);
        if (error != ESP_OK) return fail_after_write(error, "save_or_verify_timeline_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 80);
    if (pkg.has_calendar) {
        snapshot->attempted_calendar = true;
        error = save_calendar_checked(pkg.calendar);
        if (error != ESP_OK) return fail_after_write(error, "save_or_verify_calendar_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Running, 85);
    if (pkg.has_device_config) {
        snapshot->attempted_config = true;
        error = save_config_checked(pkg.device_config);
        if (error != ESP_OK) return fail_after_write(error, "save_or_verify_config_failed");
    }
    api_ptr->job_manager().update_progress(job_id, JobState::Verifying, 95);
    error = clear_marker_checked();
    if (error != ESP_OK) return fail(error, "restore_marker_clear_failed");

    api_ptr->job_manager().complete_job(job_id);
    api_ptr->command_journal().applied(cmd_id);
    api_ptr->notify_status_async();
    api::AuditLog::instance().log(api::ApiRole::Admin, "restore_complete", "Configuration and show content restored and verified");
    char response[192];
    snprintf(response, sizeof(response),
        "{\"status\":\"success\",\"accepted\":true,\"commandId\":%" PRIu32 ",\"jobId\":%" PRIu32 ",\"message\":\"Restore successful, rebooting...\"}",
        cmd_id, job_id);
    send_json(request, response);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// ── Certification Handlers (LG-037) ──────────────────────────────────────────

static esp_err_t get_certification_plan(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddNumberToObject(root, "planVersion", kCertificationPlanVersion);

    cJSON* tests_arr = cJSON_CreateArray();
    const auto* defs = CertificationPlan::test_definitions();
    size_t count = CertificationPlan::test_count();

    for (size_t i = 0; i < count; ++i) {
        cJSON* t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "id", defs[i].id);
        cJSON_AddStringToObject(t, "category", defs[i].category);
        cJSON_AddStringToObject(t, "title", defs[i].title);
        cJSON_AddStringToObject(t, "requiredEvidence", evidence_source_to_string(defs[i].required_evidence));
        cJSON_AddStringToObject(t, "reqDev", requirement_level_to_string(defs[i].req_dev));
        cJSON_AddStringToObject(t, "reqProd", requirement_level_to_string(defs[i].req_prod));
        cJSON_AddStringToObject(t, "reqRc", requirement_level_to_string(defs[i].req_rc));
        cJSON_AddNumberToObject(t, "timeoutMs", defs[i].timeout_ms);
        cJSON_AddNumberToObject(t, "expectedValue", defs[i].expected_value);
        cJSON_AddNumberToObject(t, "tolerance", defs[i].tolerance);
        cJSON_AddStringToObject(t, "unit", defs[i].unit);
        cJSON_AddItemToArray(tests_arr, t);
    }
    cJSON_AddItemToObject(root, "tests", tests_arr);

    return send_json(request, root, "200 OK");
}

static esp_err_t get_certification_status(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->certification_service()) {
        return send_error(request, "503 Service Unavailable", "CertificationService not available");
    }
    api_ptr->certification_service()->tick(esp_timer_get_time() / 1000ULL);

    const auto& s = api_ptr->certification_service()->session();
    const auto& r = api_ptr->certification_service()->record();
    bool stale = api_ptr->certification_service()->is_stale();
    const char* stale_reason = api_ptr->certification_service()->stale_reason();
    const char* cur_test = api_ptr->certification_service()->current_running_test();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddStringToObject(root, "certificationId", s.identity.certification_id);
    cJSON_AddStringToObject(root, "profile", certification_profile_to_string(s.profile));
    cJSON_AddNumberToObject(root, "planVersion", s.identity.plan_version);
    cJSON_AddStringToObject(root, "state", certification_state_to_string(s.state));
    cJSON_AddStringToObject(root, "nodeId", s.identity.node_id);
    cJSON_AddStringToObject(root, "firmwareVersion", s.identity.firmware_version);
    cJSON_AddStringToObject(root, "hardwareRevision", s.identity.hardware_revision);
    cJSON_AddStringToObject(root, "serialNumber", s.identity.serial_number);

    cJSON_AddNumberToObject(root, "passed", s.passed_count);
    cJSON_AddNumberToObject(root, "failed", s.failed_count);
    cJSON_AddNumberToObject(root, "skipped", s.skipped_count);
    cJSON_AddNumberToObject(root, "total", s.test_count);
    uint16_t pending = (s.test_count >= s.passed_count + s.failed_count + s.skipped_count)
                       ? (s.test_count - s.passed_count - s.failed_count - s.skipped_count) : 0;
    cJSON_AddNumberToObject(root, "pending", pending);

    cJSON_AddBoolToObject(root, "stale", stale);
    if (stale) {
        cJSON_AddStringToObject(root, "staleReason", stale_reason);
    }
    cJSON_AddStringToObject(root, "currentTest", cur_test ? cur_test : "");
    cJSON_AddBoolToObject(root, "cancellationPending", api_ptr->certification_service()->cancellation_pending());
    cJSON_AddBoolToObject(root, "confirmationPending", api_ptr->certification_service()->confirmation_pending());
    cJSON_AddStringToObject(root, "evidenceBinding", "session_attempt_v1");

    // Test results matrix
    cJSON* tests_arr = cJSON_CreateArray();
    for (size_t i = 0; i < r.result_count; ++i) {
        const auto& item = r.results[i];
        cJSON* t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "id", item.test_id);
        cJSON_AddStringToObject(t, "state", cert_test_state_to_string(item.state));
        cJSON_AddStringToObject(t, "evidenceSource", evidence_source_to_string(item.evidence_source));
        cJSON_AddNumberToObject(t, "startedMs", static_cast<double>(item.started_ms));
        cJSON_AddNumberToObject(t, "completedMs", static_cast<double>(item.completed_ms));
        cJSON_AddNumberToObject(t, "errorCode", item.error_code);
        cJSON_AddNumberToObject(t, "measuredValue", item.measured_value);
        cJSON_AddStringToObject(t, "unit", item.unit);
        cJSON_AddStringToObject(t, "note", item.note);
        cJSON_AddNumberToObject(t, "attempts", item.attempts);
        cJSON_AddItemToArray(tests_arr, t);
    }
    cJSON_AddItemToObject(root, "tests", tests_arr);

    return send_json(request, root, "200 OK");
}

// New clients bind every mutation to the observed session and every result to
// an execution attempt. Omitted bindings remain compatible with legacy tools;
// the service still enforces active-test ownership for those requests.
static bool certification_binding_matches(httpd_req_t* request, FactoryApi* api_ptr,
                                          const cJSON* root, const char* test_id = nullptr) {
    const cJSON* session_id = cJSON_GetObjectItemCaseSensitive(root, "certificationId");
    const cJSON* started = cJSON_GetObjectItemCaseSensitive(root, "testStartedMs");
    const cJSON* attempt = cJSON_GetObjectItemCaseSensitive(root, "testAttempt");
    if (!session_id && !started && !attempt) return true;
    if (!cJSON_IsString(session_id) || strlen(session_id->valuestring) >= sizeof(CertificationIdentity{}.certification_id)) {
        send_error(request, "400 Bad Request", "A valid certificationId binding is required");
        return false;
    }
    if (strcmp(session_id->valuestring, api_ptr->certification_service()->session().identity.certification_id) != 0) {
        send_error(request, "409 Conflict", "Certification session changed; refresh before retrying");
        return false;
    }
    if (!test_id) {
        if (started || attempt) {
            send_error(request, "400 Bad Request", "Test bindings only apply to result submissions");
            return false;
        }
        return true;
    }
    const auto valid_integer = [](const cJSON* value, double minimum, double maximum) {
        return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
            value->valuedouble >= minimum && value->valuedouble <= maximum &&
            std::floor(value->valuedouble) == value->valuedouble;
    };
    if (!valid_integer(started, 0, 9007199254740991.0) || !valid_integer(attempt, 1, UINT8_MAX)) {
        send_error(request, "400 Bad Request", "Integer testStartedMs and testAttempt bindings are required");
        return false;
    }
    const auto* result = api_ptr->certification_service()->get_test_result(test_id);
    if (!result || result->started_ms != static_cast<uint64_t>(started->valuedouble) ||
        result->attempts != static_cast<uint8_t>(attempt->valuedouble)) {
        send_error(request, "409 Conflict", "Certification test attempt changed; refresh before retrying");
        return false;
    }
    return true;
}

static esp_err_t post_certification_start(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->certification_service()) {
        return send_error(request, "503 Service Unavailable", "CertificationService not available");
    }
    api_ptr->certification_service()->tick(esp_timer_get_time() / 1000ULL);

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "A JSON object is required");
    }
    CertificationProfile profile = CertificationProfile::ProductionUnit;
    const char* serial_num = nullptr;
    const cJSON* p = cJSON_GetObjectItemCaseSensitive(root, "profile");
    const cJSON* sn = cJSON_GetObjectItemCaseSensitive(root, "serialNumber");
    if ((p && (!cJSON_IsString(p) || !parse_certification_profile(p->valuestring, profile))) ||
        (sn && (!cJSON_IsString(sn) || strlen(sn->valuestring) >= sizeof(CertificationIdentity{}.serial_number)))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Invalid profile or serial number (maximum 31 UTF-8 bytes)");
    }
    if (sn) serial_num = sn->valuestring;
    if (!certification_binding_matches(request, api_ptr, root)) { cJSON_Delete(root); return ESP_OK; }

    esp_err_t err = api_ptr->certification_service()->start_session(profile, serial_num);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        if (err == ESP_ERR_NO_MEM) return send_error(request, "503 Service Unavailable", "Certification storage unavailable");
        if (err == ESP_ERR_INVALID_STATE) return send_error(request, "409 Conflict", "Certification session is still active");
        if (err != ESP_ERR_INVALID_ARG) return send_error(request, "500 Internal Server Error", "Certification session was not persisted");
        return send_error(request, "400 Bad Request", "Failed to start certification session (check serial/hw revision)");
    }

    const auto& s = api_ptr->certification_service()->session();
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "certificationId", s.identity.certification_id);
    cJSON_AddNumberToObject(resp, "planVersion", s.identity.plan_version);
    cJSON_AddStringToObject(resp, "profile", certification_profile_to_string(s.profile));
    cJSON_AddStringToObject(resp, "state", certification_state_to_string(s.state));

    api_ptr->notify_status_async();
    return send_json(request, resp, "200 OK");
}

static esp_err_t post_certification_test_start(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->certification_service()) {
        return send_error(request, "503 Service Unavailable", "CertificationService not available");
    }
    api_ptr->certification_service()->tick(esp_timer_get_time() / 1000ULL);

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Missing JSON body");
    }

    cJSON* tid = cJSON_GetObjectItem(root, "testId");
    if (!cJSON_IsString(tid) || !tid->valuestring[0]) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Missing testId");
    }
    if (!certification_binding_matches(request, api_ptr, root)) { cJSON_Delete(root); return ESP_OK; }

    esp_err_t err = api_ptr->certification_service()->start_test(tid->valuestring);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        if (err == ESP_ERR_NO_MEM) return send_error(request, "503 Service Unavailable", "Certification storage unavailable");
        if (err == ESP_ERR_INVALID_STATE) return send_error(request, "409 Conflict", "Certification test cannot start in the current session state");
        if (err != ESP_ERR_INVALID_ARG && err != ESP_ERR_NOT_FOUND)
            return send_error(request, "500 Internal Server Error", "Certification test was not persisted");
        return send_error(request, "400 Bad Request", "Failed to start certification test");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "started");
    api_ptr->notify_status_async();
    return send_json(request, resp, "200 OK");
}

static esp_err_t post_certification_test_result(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->certification_service()) {
        return send_error(request, "503 Service Unavailable", "CertificationService not available");
    }
    api_ptr->certification_service()->tick(esp_timer_get_time() / 1000ULL);

    cJSON* root = nullptr;
    if (read_json(request, &root) != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Missing JSON body");
    }

    cJSON* tid = cJSON_GetObjectItem(root, "testId");
    if (!cJSON_IsString(tid) || !tid->valuestring[0]) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Missing testId");
    }
    const auto* definition = CertificationPlan::find_test(tid->valuestring);
    if (!definition) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Unknown certification test");
    }
    if (!certification_binding_matches(request, api_ptr, root, tid->valuestring)) { cJSON_Delete(root); return ESP_OK; }

    bool passed = false;
    cJSON* p = cJSON_GetObjectItem(root, "passed");
    if (p && !cJSON_IsBool(p)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "passed must be boolean");
    }
    if (p) passed = cJSON_IsTrue(p);

    CertificationTestState state = passed ? CertificationTestState::Passed : CertificationTestState::Failed;
    cJSON* st_item = cJSON_GetObjectItem(root, "state");
    if ((!p && !st_item) ||
        (st_item && (!cJSON_IsString(st_item) || !parse_cert_test_state(st_item->valuestring, state))) ||
        (state != CertificationTestState::Passed && state != CertificationTestState::Failed &&
         state != CertificationTestState::Skipped && state != CertificationTestState::Blocked)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Invalid terminal certification state");
    }

    EvidenceSource src = EvidenceSource::OperatorObservation;
    cJSON* src_item = cJSON_GetObjectItem(root, "evidenceSource");
    if ((src_item && (!cJSON_IsString(src_item) || !parse_evidence_source(src_item->valuestring, src))) ||
        src == EvidenceSource::Automatic || definition->required_evidence == EvidenceSource::Automatic) {
        // Automatic results are created by executed internal probes/jobs.
        // HTTP submissions represent operator observations or measurements.
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Invalid external evidence source");
    }

    float measured_val = 0.0f;
    cJSON* mv = cJSON_GetObjectItem(root, "measuredValue");
    const bool instrument_result = definition->required_evidence == EvidenceSource::InstrumentMeasurement &&
        (state == CertificationTestState::Passed || state == CertificationTestState::Failed);
    if ((instrument_result && !mv) || (mv && (!cJSON_IsNumber(mv) || !std::isfinite(mv->valuedouble)))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "A finite numeric measurement is required");
    }
    if (mv) {
        measured_val = static_cast<float>(mv->valuedouble);
        if (!std::isfinite(measured_val)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "Measurement exceeds supported range");
        }
    }

    const char* unit = "";
    cJSON* u = cJSON_GetObjectItem(root, "unit");
    if (u && (!cJSON_IsString(u) || strlen(u->valuestring) >= sizeof(CertificationTestResult{}.unit))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "unit must be a string of at most 7 UTF-8 bytes");
    }
    if (u) unit = u->valuestring;
    if (instrument_result && strcmp(unit, definition->unit) != 0) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "Measurement unit does not match the test plan");
    }

    const char* note = "";
    cJSON* n = cJSON_GetObjectItem(root, "note");
    if (n && (!cJSON_IsString(n) || strlen(n->valuestring) >= sizeof(CertificationTestResult{}.note))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "note must be a string of at most 95 UTF-8 bytes");
    }
    if (n) note = n->valuestring;

    esp_err_t err = api_ptr->certification_service()->record_result(
        tid->valuestring, state, src, measured_val, unit, note);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        if (err == ESP_ERR_NO_MEM)
            return send_error(request, "503 Service Unavailable", "Certification storage unavailable");
        if (err == ESP_ERR_INVALID_STATE)
            return send_error(request, "409 Conflict", "Certification test is no longer waiting for this evidence");
        if (err != ESP_ERR_INVALID_ARG && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NOT_FOUND)
            return send_error(request, "500 Internal Server Error", "Certification evidence was not persisted");
        return send_error(request, "400 Bad Request", "Failed to record test result (validation / anti-spoof check failed)");
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "recorded");
    cJSON_AddStringToObject(resp, "overallState", certification_state_to_string(api_ptr->certification_service()->session().state));
    api_ptr->notify_status_async();
    return send_json(request, resp, "200 OK");
}

static esp_err_t post_certification_abort(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Admin)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->certification_service()) {
        return send_error(request, "503 Service Unavailable", "CertificationService not available");
    }
    api_ptr->certification_service()->tick(esp_timer_get_time() / 1000ULL);

    // Empty bodies remain valid for existing service tools.
    if (request->content_len > 0) {
        cJSON* root = nullptr;
        if (read_json(request, &root) != ESP_OK || !cJSON_IsObject(root)) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", "A JSON object is required");
        }
        const bool matches = certification_binding_matches(request, api_ptr, root);
        cJSON_Delete(root);
        if (!matches) return ESP_OK;
    }
    const esp_err_t err = api_ptr->certification_service()->abort();
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            return send_error(request, "409 Conflict", "Certification cancellation was not queued");
        }
        return send_error(request, "500 Internal Server Error", "Certification abort was not persisted");
    }
    const bool pending = api_ptr->certification_service()->cancellation_pending();
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", pending ? "cancellation_requested" : "aborted");
    if (pending) {
        cJSON_AddBoolToObject(resp, "accepted", true);
        cJSON_AddBoolToObject(resp, "cancellationRequested", true);
    }
    api_ptr->notify_status_async();
    return send_json(request, resp, pending ? "202 Accepted" : "200 OK");
}

static esp_err_t get_certification_report(httpd_req_t* request) {
    if (!api::authorize(request, api::ApiRole::Operator)) return ESP_OK;

    FactoryApi* api_ptr = static_cast<FactoryApi*>(request->user_ctx);
    if (!api_ptr->certification_service()) {
        return send_error(request, "503 Service Unavailable", "CertificationService not available");
    }
    api_ptr->certification_service()->tick(esp_timer_get_time() / 1000ULL);

    // Fixed report headers plus one test tree/string at a time. Never retain
    // all test objects and their serialized document simultaneously.
    constexpr uint32_t scratch = 8192;
    if (!check_memory_admission(scratch).accepted) return send_memory_admission_error(request, scratch);
    auto* service = api_ptr->certification_service();
    cJSON* headers = service->generate_report_json(false);
    char* body = cJSON_PrintUnformatted(headers);
    cJSON_Delete(headers);
    if (!body) { note_api_allocation_failure(); return ESP_ERR_NO_MEM; }
    std::unique_ptr<char, decltype(&cJSON_free)> owned_body(body, cJSON_free);
    char* tests = strstr(body, "\"tests\":[]");
    if (!tests) return ESP_FAIL;
    char* suffix = tests + strlen("\"tests\":[");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(request, body, suffix - body) != ESP_OK) return ESP_FAIL;
    const size_t count = service->record().result_count;
    for (size_t i = 0; i < count; ++i) {
        cJSON* result = service->generate_report_result_json(i);
        char* json = cJSON_PrintUnformatted(result);
        cJSON_Delete(result);
        if (!json) { note_api_allocation_failure(); return ESP_ERR_NO_MEM; }
        std::unique_ptr<char, decltype(&cJSON_free)> owned_result(json, cJSON_free);
        if (i && httpd_resp_send_chunk(request, ",", 1) != ESP_OK) return ESP_FAIL;
        if (httpd_resp_send_chunk(request, json, strlen(json)) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(request, suffix, strlen(suffix)) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(request, nullptr, 0);
}

struct WsBroadcastArg {
    FactoryApi* api;
    char payload[kRuntimeSnapshotMaxBytes];
};

// ── LG-038.3 §52/§53/§54: bounded, downsampled binary preview ──────────────
// Preview is UI telemetry, NOT hardware truth. A 256-pixel configured lane is
// sampled down to at most kPreviewSamplesPerLane samples per lane so the
// payload stays ~1.5 KB regardless of configured pixel count. The header
// declares schema + lane count + samples-per-lane; the frontend must treat
// sample indices as preview samples, never physical pixel indices.
static constexpr uint8_t  kPreviewSchema          = 2;
static constexpr uint8_t  kPreviewLanes           = 8;
static constexpr uint16_t kPreviewSamplesPerLane  = 64;
static constexpr size_t   kPreviewHeaderBytes     = 4;
static constexpr size_t   kPreviewLaneBytes       = kPreviewSamplesPerLane * 3;
static constexpr size_t   kPreviewMaxBytes        =
    kPreviewHeaderBytes + kPreviewLanes * kPreviewLaneBytes; // 4 + 8*64*3 = 1540

struct WsPreviewArg {
    FactoryApi* api;
    uint16_t len;
    std::array<uint8_t, kPreviewMaxBytes> payload;
};

// Fixed, bounded client-list buffer. max_open_sockets_ is already bounded by
// the IDF config; no std::vector allocation per broadcast (§55, §82).
static constexpr size_t kMaxClientListFds = 16;

// Samples the physical frame for one lane into the bounded preview buffer.
// Downsample by stepping across the full configured length; W folds into RGB
// for visualization only.
inline void preview_sample_lane(uint8_t* out, size_t out_cap,
                                const RenderFrame& frame, uint8_t lane,
                                uint16_t configured_pixels) {
    if (configured_pixels == 0) return; // lane stays black in the payload
    const auto& ch_frame = frame.channels[lane];
    for (uint16_t s = 0; s < kPreviewSamplesPerLane; ++s) {
        const uint16_t px = static_cast<uint16_t>(
            (static_cast<uint32_t>(s) * (configured_pixels - 1)) / (kPreviewSamplesPerLane - 1));
        if (px >= MAX_PIXELS) break;
        const auto& lv = ch_frame.pixels[px];
        const size_t off = static_cast<size_t>(s) * 3;
        if (off + 2 >= out_cap) break;
        out[off + 0] = static_cast<uint8_t>((std::min)(255, static_cast<int>(lv.r) + lv.w));
        out[off + 1] = static_cast<uint8_t>((std::min)(255, static_cast<int>(lv.g) + lv.w));
        out[off + 2] = static_cast<uint8_t>((std::min)(255, static_cast<int>(lv.b) + lv.w));
    }
}

static esp_err_t get_favicon(httpd_req_t* request) {
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, nullptr, 0);
}

static esp_err_t options_handler(httpd_req_t* request) {
    httpd_resp_set_status(request, "204 No Content");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id");
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
    std::unique_ptr<DeviceConfig> cfg_storage(new (std::nothrow) DeviceConfig{});
    if (!cfg_storage) return ESP_ERR_NO_MEM;
    DeviceConfig &cfg = *cfg_storage;
    if (config_) {
        const esp_err_t cfg_err = config_->load_config(cfg);
        if (cfg_err != ESP_OK) return cfg_err;
        dev_name = cfg.device_name;
    }
    identity_ = get_node_identity(dev_name);
    cfg_storage.reset();

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    static const httpd_uri_t factory_routes[] = {
        // ── Factory test endpoints ──
        { .uri = "/api/v1/factory/status",         .method = HTTP_GET,  .handler = get_status,                 .user_ctx = nullptr },
        { .uri = "/api/v1/factory/effect",         .method = HTTP_POST, .handler = post_effect,                .user_ctx = nullptr },
        { .uri = "/api/v1/factory/lane",           .method = HTTP_POST, .handler = post_lane,                  .user_ctx = nullptr },
        { .uri = "/api/v1/factory/pwm",            .method = HTTP_POST, .handler = post_pwm,                   .user_ctx = nullptr },
        { .uri = "/api/v1/factory/abort",          .method = HTTP_POST, .handler = post_abort,                 .user_ctx = nullptr },
        { .uri = "/api/v1/factory/auto-loop",      .method = HTTP_POST, .handler = post_auto_loop,             .user_ctx = nullptr },
        { .uri = "/api/v1/factory/test/start",     .method = HTTP_POST, .handler = post_factory_test_start,    .user_ctx = nullptr },
        { .uri = "/api/v1/factory/test/confirm",   .method = HTTP_POST, .handler = post_factory_test_confirm,  .user_ctx = nullptr },
        { .uri = "/api/v1/factory/test/abort",     .method = HTTP_POST, .handler = post_factory_test_abort,    .user_ctx = nullptr },
        { .uri = "/api/v1/factory/test/report",    .method = HTTP_GET,  .handler = get_factory_test_report,    .user_ctx = nullptr },
        { .uri = "/api/v1/factory/burn-in/start",  .method = HTTP_POST, .handler = post_factory_burn_in_start,  .user_ctx = nullptr },
        { .uri = "/api/v1/factory/burn-in/stop",   .method = HTTP_POST, .handler = post_factory_burn_in_stop,   .user_ctx = nullptr },
        { .uri = "/api/v1/factory/boot-safe",      .method = HTTP_GET,  .handler = get_boot_safe_verify,        .user_ctx = nullptr },
    };

    static const httpd_uri_t signage_routes[] = {
        // ── WebSocket + Dashboard + Preflight ──
        { .uri = "/ws",         .method = HTTP_GET,     .handler = ws_handler,      .user_ctx = nullptr,
          .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr },
        { .uri = "/",           .method = HTTP_GET,     .handler = serve_dashboard, .user_ctx = nullptr },
        { .uri = "/index.html", .method = HTTP_GET,     .handler = serve_dashboard, .user_ctx = nullptr },
        { .uri = "/dashboard",  .method = HTTP_GET,     .handler = serve_dashboard, .user_ctx = nullptr },
        { .uri = "/favicon.ico",.method = HTTP_GET,     .handler = get_favicon,     .user_ctx = nullptr },
        { .uri = "/api/v1/*",   .method = HTTP_OPTIONS, .handler = options_handler, .user_ctx = nullptr },
        
        // ── Identity & Info ──
        { .uri = "/api/v1/info",              .method = HTTP_GET,  .handler = get_info,           .user_ctx = nullptr },
        { .uri = "/api/v1/identity",          .method = HTTP_GET,  .handler = get_identity,       .user_ctx = nullptr },
        { .uri = "/api/v1/fleet/nodes",       .method = HTTP_GET,  .handler = get_fleet_nodes,    .user_ctx = nullptr },

        // ── Auth endpoints (LG-029) ──
        { .uri = "/api/v1/auth/status",       .method = HTTP_GET,  .handler = get_auth_status,   .user_ctx = nullptr },
        { .uri = "/api/v1/auth/bootstrap",    .method = HTTP_POST, .handler = post_auth_bootstrap,.user_ctx = nullptr },
        { .uri = "/api/v1/auth/session",      .method = HTTP_POST, .handler = post_auth_session,  .user_ctx = nullptr },
        { .uri = "/api/v1/auth/session",      .method = HTTP_GET,  .handler = get_auth_session,   .user_ctx = nullptr },
        { .uri = "/api/v1/auth/session",      .method = HTTP_DELETE,.handler = delete_auth_session,.user_ctx = nullptr },
        { .uri = "/api/v1/auth/ws-ticket",    .method = HTTP_POST, .handler = post_auth_ws_ticket,.user_ctx = nullptr },
        { .uri = "/api/v1/auth/credentials",  .method = HTTP_GET,  .handler = get_auth_credentials,.user_ctx = nullptr },
        { .uri = "/api/v1/auth/credentials",  .method = HTTP_POST, .handler = post_auth_credentials,.user_ctx = nullptr },
        { .uri = "/api/v1/auth/credentials",  .method = HTTP_DELETE,.handler = delete_auth_credentials,.user_ctx = nullptr },

        // ── Signage endpoints ──
        { .uri = "/api/v1/capabilities",      .method = HTTP_GET,  .handler = get_capabilities,   .user_ctx = nullptr },
        { .uri = "/api/v1/jobs/current",      .method = HTTP_GET,  .handler = get_current_job,    .user_ctx = nullptr },
        { .uri = "/api/v1/channels",          .method = HTTP_GET,  .handler = get_channels,       .user_ctx = nullptr },
        { .uri = "/api/v1/channel/test",      .method = HTTP_POST, .handler = post_channel_test,  .user_ctx = nullptr },
        { .uri = "/api/v1/presets/references",.method = HTTP_GET,  .handler = get_preset_references, .user_ctx = nullptr },
        { .uri = "/api/v1/presets",           .method = HTTP_GET,  .handler = get_presets,      .user_ctx = nullptr },
        { .uri = "/api/v1/presets",           .method = HTTP_PUT,  .handler = put_preset,       .user_ctx = nullptr },
        { .uri = "/api/v1/presets",           .method = HTTP_DELETE,.handler = delete_preset,    .user_ctx = nullptr },
        { .uri = "/api/v1/activate",          .method = HTTP_POST, .handler = post_activate,    .user_ctx = nullptr },
        { .uri = "/api/v1/config",            .method = HTTP_GET,  .handler = get_device_config, .user_ctx = nullptr },
        { .uri = "/api/v1/config",            .method = HTTP_PUT,  .handler = put_device_config, .user_ctx = nullptr },
        { .uri = "/api/v1/config/validate",   .method = HTTP_POST, .handler = post_config_validate, .user_ctx = nullptr },
        { .uri = "/api/v1/network/config",    .method = HTTP_GET,  .handler = get_network_config, .user_ctx = nullptr },
        { .uri = "/api/v1/network/config",    .method = HTTP_PUT,  .handler = put_network_config, .user_ctx = nullptr },
        { .uri = "/api/v1/network/status",    .method = HTTP_GET,  .handler = get_network_status, .user_ctx = nullptr },
        { .uri = "/api/v1/status",            .method = HTTP_GET,  .handler = get_signage_status, .user_ctx = nullptr },
        { .uri = "/api/v1/dmx/frame",         .method = HTTP_GET,  .handler = get_dmx_frame,      .user_ctx = nullptr },
        { .uri = "/api/v1/manual",            .method = HTTP_PUT,   .handler = put_manual,        .user_ctx = nullptr },
        { .uri = "/api/v1/manual",            .method = HTTP_DELETE,.handler = delete_manual,     .user_ctx = nullptr },
        { .uri = "/api/v1/effects",           .method = HTTP_GET,  .handler = get_effects,        .user_ctx = nullptr },
        { .uri = "/api/v1/palettes",          .method = HTTP_GET,  .handler = get_palettes,       .user_ctx = nullptr },
        { .uri = "/api/v1/health",            .method = HTTP_GET,  .handler = get_health,         .user_ctx = nullptr },
        { .uri = "/api/v1/diagnostics/output",.method = HTTP_GET,  .handler = get_diagnostics_output, .user_ctx = nullptr },
        { .uri = "/api/v1/diagnostics/storage", .method = HTTP_GET, .handler = get_diagnostics_storage, .user_ctx = nullptr },
        { .uri = "/api/v1/diagnostics/memory", .method = HTTP_GET,  .handler = get_diagnostics_memory, .user_ctx = nullptr },
        { .uri = "/api/v1/logs",              .method = HTTP_GET,  .handler = get_logs,           .user_ctx = nullptr },
        { .uri = "/api/v1/sequence",          .method = HTTP_GET,  .handler = get_sequence,     .user_ctx = nullptr },
        { .uri = "/api/v1/sequence",          .method = HTTP_PUT,  .handler = put_sequence,     .user_ctx = nullptr },
        { .uri = "/api/v1/schedules",         .method = HTTP_GET,  .handler = get_calendar_schedule, .user_ctx = nullptr },
        { .uri = "/api/v1/schedules",         .method = HTTP_PUT,  .handler = put_calendar_schedule, .user_ctx = nullptr },
        { .uri = "/api/v1/scene-motion",      .method = HTTP_GET,  .handler = get_scene_motion, .user_ctx = nullptr },
        { .uri = "/api/v1/scene-motion",      .method = HTTP_PUT,  .handler = put_scene_motion, .user_ctx = nullptr },
        { .uri = "/api/v1/scene-motion",      .method = HTTP_DELETE,.handler = delete_scene_motion, .user_ctx = nullptr },
        { .uri = "/api/v1/cue-lists",         .method = HTTP_GET,  .handler = get_cue_lists,    .user_ctx = nullptr },
        { .uri = "/api/v1/cue-list",          .method = HTTP_GET,  .handler = get_cue_list,     .user_ctx = nullptr },
        { .uri = "/api/v1/cue-list",          .method = HTTP_PUT,  .handler = put_cue_list,     .user_ctx = nullptr },
        { .uri = "/api/v1/cue-list",          .method = HTTP_DELETE,.handler = delete_cue_list,  .user_ctx = nullptr },
        { .uri = "/api/v1/cue/control",       .method = HTTP_POST, .handler = post_cue_control, .user_ctx = nullptr },
        { .uri = "/api/v1/cue/status",        .method = HTTP_GET,  .handler = get_cue_status,   .user_ctx = nullptr },
        { .uri = "/api/v1/show/status",       .method = HTTP_GET,  .handler = get_show_status,   .user_ctx = nullptr },
        { .uri = "/api/v1/show/control",      .method = HTTP_POST, .handler = post_show_control,  .user_ctx = nullptr },
        { .uri = "/api/v1/sync",              .method = HTTP_GET,  .handler = get_sync_status,     .user_ctx = nullptr },
        { .uri = "/api/v1/show-manifest",     .method = HTTP_GET,  .handler = get_show_manifest, .user_ctx = nullptr },
        { .uri = "/api/v1/audit",             .method = HTTP_GET,  .handler = get_audit_log,       .user_ctx = nullptr },
        { .uri = "/api/v1/ota",               .method = HTTP_POST, .handler = post_ota,           .user_ctx = nullptr },
        { .uri = "/api/v1/ota/status",        .method = HTTP_GET,  .handler = get_ota_status,       .user_ctx = nullptr },
        { .uri = "/api/v1/recovery/status",   .method = HTTP_GET,  .handler = get_recovery_status,  .user_ctx = nullptr },
        { .uri = "/api/v1/backup",            .method = HTTP_GET,  .handler = get_backup,         .user_ctx = nullptr },
        { .uri = "/api/v1/restore/validate",  .method = HTTP_POST, .handler = post_restore_validate, .user_ctx = nullptr },
        { .uri = "/api/v1/restore",           .method = HTTP_POST, .handler = post_restore,       .user_ctx = nullptr },
        { .uri = "/api/v1/certification/plan",        .method = HTTP_GET,  .handler = get_certification_plan,        .user_ctx = nullptr },
        { .uri = "/api/v1/certification/status",      .method = HTTP_GET,  .handler = get_certification_status,      .user_ctx = nullptr },
        { .uri = "/api/v1/certification/start",       .method = HTTP_POST, .handler = post_certification_start,       .user_ctx = nullptr },
        { .uri = "/api/v1/certification/test/start",  .method = HTTP_POST, .handler = post_certification_test_start,  .user_ctx = nullptr },
        { .uri = "/api/v1/certification/test/result", .method = HTTP_POST, .handler = post_certification_test_result, .user_ctx = nullptr },
        { .uri = "/api/v1/certification/abort",       .method = HTTP_POST, .handler = post_certification_abort,       .user_ctx = nullptr },
        { .uri = "/api/v1/certification/report",      .method = HTTP_GET,  .handler = get_certification_report,      .user_ctx = nullptr },
    };


    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    // Keep capacity aligned with the actual registered routes.
    http_config.max_uri_handlers = sizeof(factory_routes) / sizeof(factory_routes[0]) +
                                   sizeof(signage_routes) / sizeof(signage_routes[0]);
    http_config.stack_size = 8192;
    http_config.lru_purge_enable = true;
    http_config.recv_wait_timeout = 10;
    http_config.send_wait_timeout = 10;
    http_config.server_port = signage_port;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_RETURN_ON_ERROR(httpd_start(&signage_server_, &http_config), "factory-api", "start server");
    task_stack_registry().register_task("httpd", http_config.stack_size);
    factory_server_ = signage_server_;

    for (const auto& endpoint : factory_routes) {
        httpd_uri_t route = endpoint;
        if (route.handler != options_handler) route.user_ctx = this;
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(signage_server_, &route),
            "api",
            "register uri"
        );
    }
    for (const auto& endpoint : signage_routes) {
        httpd_uri_t route = endpoint;
        if (route.handler != options_handler) route.user_ctx = this;
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(signage_server_, &route),
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
            size_t client_count = kMaxClientListFds;
            int client_fds[kMaxClientListFds];
            if (httpd_get_client_list(this->factory_server_, &client_count, client_fds) != ESP_OK || client_count == 0) {
                this->preview_pending_ = false;
                return;
            }

            auto* arg = new (std::nothrow) WsPreviewArg{};
            if (!arg) {
                // §57: allocation failures are counted, never silently dropped.
                note_preview_allocation_failure();
                this->preview_pending_ = false;
                return;
            }
            arg->api = this;

            // Fixed-size bounded payload (§54): no vector, no resize, no
            // variable-size payload. One coalesced allocation owns the queued frame.
            arg->len = static_cast<uint16_t>(kPreviewMaxBytes);
            arg->payload[0] = kPreviewSchema;                       // schema
            arg->payload[1] = kPreviewLanes;                        // lane count
            arg->payload[2] = static_cast<uint8_t>(kPreviewSamplesPerLane); // samples per lane
            arg->payload[3] = 0;                                    // reserved

            for (uint8_t f = 0; f < kPreviewLanes; ++f) {
                const auto& ch_cfg = cfg.channels[f];
                uint8_t* lane_out = arg->payload.data() + kPreviewHeaderBytes + f * kPreviewLaneBytes;
                preview_sample_lane(lane_out, kPreviewLaneBytes, frame, f,
                                    ch_cfg.enabled ? ch_cfg.pixel_count : 0);
            }

            if (httpd_queue_work(this->factory_server_, ws_broadcast_preview_work, arg) != ESP_OK) {
                note_preview_allocation_failure();
                this->preview_pending_ = false;
                delete arg;
            }
        });
    }

    return ESP_OK;
}

void FactoryApi::ws_broadcast_work(void* arg) {
    std::unique_ptr<WsBroadcastArg> msg(static_cast<WsBroadcastArg*>(arg));

    // Certification mutations and NVS persistence share the HTTP owner with
    // its request handlers. The periodic status work also runs without a WS
    // client, keeping timeouts and burn-in completion off the lighting task.
    if (msg->api->cert_service_) {
        msg->api->cert_service_->tick(esp_timer_get_time() / 1000ULL);
    }

    // LG-038.3 §55/§82: fixed bounded array; no heap vector per broadcast.
    int client_fds[kMaxClientListFds];
    size_t clients = kMaxClientListFds;

    if (httpd_get_client_list(msg->api->factory_server_, &clients, client_fds) != ESP_OK || clients == 0) {
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
    struct Complete { FactoryApi* api; ~Complete() { api->preview_pending_.store(false); } } complete{msg->api};

    // LG-038.3 §55/§82: fixed bounded array; no heap vector per broadcast.
    int client_fds[kMaxClientListFds];
    size_t clients = kMaxClientListFds;

    if (httpd_get_client_list(msg->api->factory_server_, &clients, client_fds) != ESP_OK || clients == 0) {
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = msg->payload.data();
    frame.len = msg->len;

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
        note_api_allocation_failure();
        status_pending_.store(false);
        return;
    }
    arg->api = this;
    arg->payload[0] = '\0';

    if (httpd_queue_work(factory_server_, ws_broadcast_work, arg) != ESP_OK) {
        note_api_allocation_failure();
        status_pending_.store(false);
        delete arg;
    }
}

void FactoryApi::queue_service_reconcile() {
    if (service_reconcile_callback_) {
        service_reconcile_callback_();
    }
}

void FactoryApi::stop() {
    if (signage_server_ != nullptr) {
        httpd_stop(signage_server_);
        signage_server_ = nullptr;
        factory_server_ = nullptr;
    }
}

uint32_t FactoryApi::active_ws_clients() const {
    httpd_handle_t srv = signage_server_ ? signage_server_ : factory_server_;
    if (!srv) return 0;
    // LG-038.3 §56/§82: routine client counting must not heap-allocate.
    int client_fds[kMaxClientListFds];
    size_t clients = kMaxClientListFds;
    if (httpd_get_client_list(srv, &clients, client_fds) != ESP_OK || clients == 0) return 0;
    size_t ws_clients = 0;
    for (size_t i = 0; i < clients; ++i) {
        if (httpd_ws_get_fd_info(srv, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            ws_clients++;
        }
    }
    return static_cast<uint32_t>(ws_clients);
}

} // namespace lg
