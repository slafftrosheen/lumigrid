#include "FactoryApi.hpp"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>
#include <string>

namespace lg {

static const char* TAG = "FactoryApi";

// Utility to read JSON from HTTP request
static cJSON* parse_json_request(httpd_req_t *req) {
    int total_len = req->content_len;
    int cur_len = 0;
    char* buf = (char*)malloc(total_len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        return nullptr;
    }

    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            return nullptr;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON* json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        ESP_LOGE(TAG, "Invalid JSON received");
    }
    return json;
}

FactoryApi::FactoryApi(FactoryRunner& runner) : runner_(runner) {}

FactoryApi::~FactoryApi() {
    stop();
}

esp_err_t FactoryApi::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server_, &config) == ESP_OK) {
        httpd_uri_t uri_post_effect = {
            .uri       = "/api/v1/factory/effect",
            .method    = HTTP_POST,
            .handler   = post_effect_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_post_effect);

        httpd_uri_t uri_post_stop = {
            .uri       = "/api/v1/factory/stop",
            .method    = HTTP_POST,
            .handler   = post_stop_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_post_stop);

        httpd_uri_t uri_get_status = {
            .uri       = "/api/v1/factory/status",
            .method    = HTTP_GET,
            .handler   = get_status_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_get_status);

        httpd_uri_t uri_post_pwm = {
            .uri       = "/api/v1/factory/pwm/*",
            .method    = HTTP_POST,
            .handler   = post_pwm_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_post_pwm);

        httpd_uri_t uri_post_lane = {
            .uri       = "/api/v1/factory/lane/*",
            .method    = HTTP_POST,
            .handler   = post_lane_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_post_lane);

        httpd_uri_t uri_post_burn_in = {
            .uri       = "/api/v1/factory/burn-in",
            .method    = HTTP_POST,
            .handler   = post_burn_in_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_post_burn_in);

        httpd_uri_t uri_post_oe = {
            .uri       = "/api/v1/factory/oe",
            .method    = HTTP_POST,
            .handler   = post_oe_handler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_, &uri_post_oe);

        return ESP_OK;
    }
    return ESP_FAIL;
}

void FactoryApi::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

esp_err_t FactoryApi::post_effect_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    cJSON* json = parse_json_request(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON* name_item = cJSON_GetObjectItem(json, "name");
    cJSON* duration_item = cJSON_GetObjectItem(json, "duration_ms");
    cJSON* brightness_item = cJSON_GetObjectItem(json, "brightness");

    if (!cJSON_IsString(name_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'name'");
        return ESP_OK;
    }

    uint32_t duration = duration_item && cJSON_IsNumber(duration_item) ? duration_item->valueint : 10000;
    uint8_t brightness = brightness_item && cJSON_IsNumber(brightness_item) ? brightness_item->valueint : LG_FACTORY_DEFAULT_BRIGHTNESS;

    std::string name = name_item->valuestring;
    FactoryEffect effect = FactoryEffect::SafeBlackout;

    if (name == "identity") effect = FactoryEffect::Identity;
    else if (name == "pixel_chase") effect = FactoryEffect::PixelChase;
    else if (name == "rainbow") effect = FactoryEffect::Rainbow;
    else if (name == "white_walk") effect = FactoryEffect::WhiteWalk;
    else if (name == "pwm_walk") effect = FactoryEffect::PwmWalk;
    else if (name == "pwm_fade") effect = FactoryEffect::PwmFade;
    else if (name == "combined_pulse") effect = FactoryEffect::CombinedPulse;
    else {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown effect name");
        return ESP_OK;
    }

    api->runner_.select(effect, duration, brightness);
    cJSON_Delete(json);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t FactoryApi::post_stop_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    api->runner_.stop();
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t FactoryApi::get_status_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"active_effect\":\"%s\"}", effect_name(api->runner_.active_effect()));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t FactoryApi::post_pwm_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    
    // Extract channel from URI /api/v1/factory/pwm/X
    const char* p = strrchr(req->uri, '/');
    if (!p) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid channel");
        return ESP_OK;
    }
    int channel = atoi(p + 1);

    cJSON* json = parse_json_request(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON* duty_item = cJSON_GetObjectItem(json, "duty");
    if (!cJSON_IsNumber(duty_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'duty'");
        return ESP_OK;
    }

    api->runner_.select(FactoryEffect::Manual, 0, 0); // Disable auto-loop / clears
    api->runner_.output().pwm().set(channel, duty_item->valueint);
    api->runner_.output().commit();
    cJSON_Delete(json);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t FactoryApi::post_lane_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    
    const char* p = strrchr(req->uri, '/');
    if (!p) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid lane");
        return ESP_OK;
    }
    int lane = atoi(p + 1);

    cJSON* json = parse_json_request(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON* r_item = cJSON_GetObjectItem(json, "r");
    cJSON* g_item = cJSON_GetObjectItem(json, "g");
    cJSON* b_item = cJSON_GetObjectItem(json, "b");

    if (!cJSON_IsNumber(r_item) || !cJSON_IsNumber(g_item) || !cJSON_IsNumber(b_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'r', 'g', or 'b'");
        return ESP_OK;
    }

    api->runner_.select(FactoryEffect::Manual, 0, 0);
    api->runner_.output().pixels().fill(lane, {
        static_cast<uint8_t>(r_item->valueint),
        static_cast<uint8_t>(g_item->valueint),
        static_cast<uint8_t>(b_item->valueint)
    });
    api->runner_.output().commit();
    cJSON_Delete(json);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t FactoryApi::post_burn_in_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    
    cJSON* json = parse_json_request(req);
    uint32_t duration = 3600000; // 1 hour default
    uint8_t brightness = LG_FACTORY_DEFAULT_BRIGHTNESS;

    if (json) {
        cJSON* duration_item = cJSON_GetObjectItem(json, "duration_ms");
        cJSON* brightness_item = cJSON_GetObjectItem(json, "brightness");
        if (cJSON_IsNumber(duration_item)) duration = duration_item->valueint;
        if (cJSON_IsNumber(brightness_item)) brightness = brightness_item->valueint;
        cJSON_Delete(json);
    }

    api->runner_.select(FactoryEffect::BurnIn, duration, brightness);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t FactoryApi::post_oe_handler(httpd_req_t *req) {
    auto* api = static_cast<FactoryApi*>(req->user_ctx);
    
    cJSON* json = parse_json_request(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON* enable_item = cJSON_GetObjectItem(json, "enable");
    if (cJSON_IsBool(enable_item) || cJSON_IsNumber(enable_item)) {
        bool enable = cJSON_IsTrue(enable_item) || (cJSON_IsNumber(enable_item) && enable_item->valueint != 0);
        if (enable) {
            api->runner_.output().enable();
        } else {
            api->runner_.output().disable();
        }
    }
    cJSON_Delete(json);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

} // namespace lg
