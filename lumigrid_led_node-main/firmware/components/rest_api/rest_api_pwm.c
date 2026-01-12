#include "rest_api.h"
#include "pwm_effects.h"
#include "pca9685_driver.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "REST_PWM";

// Helper to receive and parse JSON body
static esp_err_t recv_json_body(httpd_req_t *req, cJSON **json_out) {
    char buf[1024];
    size_t content_len = req->content_len;
    
    if (content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }
    
    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    *json_out = cJSON_Parse(buf);
    if (!*json_out) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// POST /api/pwm/{channel} - Set channel duty cycle
esp_err_t pwm_set_duty_handler(httpd_req_t *req) {
    // Parse channel from URI
    const char *uri = req->uri;
    int channel = -1;
    if (sscanf(uri, "/api/pwm/%d", &channel) != 1 || channel < 0 || channel >= 16) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid channel");
        return ESP_FAIL;
    }
    
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }
    
    cJSON *duty_obj = cJSON_GetObjectItem(json, "duty");
    if (!duty_obj || !cJSON_IsNumber(duty_obj)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'duty'");
        return ESP_FAIL;
    }
    
    float duty = (float)duty_obj->valuedouble;
    cJSON_Delete(json);
    
    esp_err_t err = pca9685_set_duty(channel, duty);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "PWM error");
        return ESP_FAIL;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "channel", channel);
    cJSON_AddNumberToObject(resp, "duty", duty);
    cJSON_AddStringToObject(resp, "status", "ok");
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// POST /api/pwm/{channel}/waveform - Start waveform generator
esp_err_t pwm_waveform_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    int channel = -1;
    if (sscanf(uri, "/api/pwm/%d/waveform", &channel) != 1 || channel < 0 || channel >= 16) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid channel");
        return ESP_FAIL;
    }
    
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }
    
    waveform_config_t config = {0};
    
    // Parse waveform type
    cJSON *type_obj = cJSON_GetObjectItem(json, "type");
    if (type_obj && cJSON_IsString(type_obj)) {
        const char *type_str = type_obj->valuestring;
        if (strcmp(type_str, "sine") == 0) config.type = WAVE_SINE;
        else if (strcmp(type_str, "triangle") == 0) config.type = WAVE_TRIANGLE;
        else if (strcmp(type_str, "sawtooth") == 0) config.type = WAVE_SAWTOOTH;
        else if (strcmp(type_str, "square") == 0) config.type = WAVE_SQUARE;
        else if (strcmp(type_str, "pulse") == 0) config.type = WAVE_PULSE;
        else config.type = WAVE_SINE;
    }
    
    cJSON *freq_obj = cJSON_GetObjectItem(json, "frequency_hz");
    if (freq_obj && cJSON_IsNumber(freq_obj)) {
        config.frequency_hz = (float)freq_obj->valuedouble;
    } else {
        config.frequency_hz = 1.0f;
    }
    
    cJSON *amp_obj = cJSON_GetObjectItem(json, "amplitude");
    if (amp_obj && cJSON_IsNumber(amp_obj)) {
        config.amplitude = (float)amp_obj->valuedouble;
    } else {
        config.amplitude = 1.0f;
    }
    
    cJSON *offset_obj = cJSON_GetObjectItem(json, "offset");
    if (offset_obj && cJSON_IsNumber(offset_obj)) {
        config.offset = (float)offset_obj->valuedouble;
    } else {
        config.offset = 0.0f;
    }
    
    cJSON *duty_obj = cJSON_GetObjectItem(json, "duty_cycle");
    if (duty_obj && cJSON_IsNumber(duty_obj)) {
        config.duty_cycle = (float)duty_obj->valuedouble;
    } else {
        config.duty_cycle = 0.5f;
    }
    
    cJSON_Delete(json);
    
    esp_err_t err = pwm_waveform_create(channel, &config);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Waveform error");
        return ESP_FAIL;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "waveform", "started");
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// POST /api/pwm/{channel}/effect/candle - Start candle effect
esp_err_t pwm_candle_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    int channel = -1;
    if (sscanf(uri, "/api/pwm/%d/effect/candle", &channel) != 1 || 
        channel < 0 || channel >= 16) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid channel");
        return ESP_FAIL;
    }
    
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }
    
    pwm_candle_t params = {
        .base_brightness = 0.7f,
        .flicker_amount = 0.3f,
        .flicker_speed = 8.0f,
        .seed = 0
    };
    
    cJSON *base_obj = cJSON_GetObjectItem(json, "base_brightness");
    if (base_obj && cJSON_IsNumber(base_obj)) {
        params.base_brightness = (float)base_obj->valuedouble;
    }
    
    cJSON *flicker_obj = cJSON_GetObjectItem(json, "flicker_amount");
    if (flicker_obj && cJSON_IsNumber(flicker_obj)) {
        params.flicker_amount = (float)flicker_obj->valuedouble;
    }
    
    cJSON *speed_obj = cJSON_GetObjectItem(json, "flicker_speed");
    if (speed_obj && cJSON_IsNumber(speed_obj)) {
        params.flicker_speed = (float)speed_obj->valuedouble;
    }
    
    cJSON_Delete(json);
    
    esp_err_t err = pwm_candle_start(channel, &params);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Effect error");
        return ESP_FAIL;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "effect", "candle");
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// POST /api/pwm/{channel}/effect/fade - Start fade effect
esp_err_t pwm_fade_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    int channel = -1;
    if (sscanf(uri, "/api/pwm/%d/effect/fade", &channel) != 1 || 
        channel < 0 || channel >= 16) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid channel");
        return ESP_FAIL;
    }
    
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }
    
    pwm_fade_t params = {
        .duration_ms = 5000,
        .start_brightness = 0.0f,
        .end_brightness = 1.0f,
        .curve = 0,
        .active = false
    };
    
    cJSON *duration_obj = cJSON_GetObjectItem(json, "duration_ms");
    if (duration_obj && cJSON_IsNumber(duration_obj)) {
        params.duration_ms = (uint32_t)duration_obj->valueint;
    }
    
    cJSON *start_obj = cJSON_GetObjectItem(json, "start_brightness");
    if (start_obj && cJSON_IsNumber(start_obj)) {
        params.start_brightness = (float)start_obj->valuedouble;
    }
    
    cJSON *end_obj = cJSON_GetObjectItem(json, "end_brightness");
    if (end_obj && cJSON_IsNumber(end_obj)) {
        params.end_brightness = (float)end_obj->valuedouble;
    }
    
    cJSON *curve_obj = cJSON_GetObjectItem(json, "curve");
    if (curve_obj && cJSON_IsNumber(curve_obj)) {
        params.curve = (uint8_t)curve_obj->valueint;
    }
    
    cJSON_Delete(json);
    
    esp_err_t err = pwm_fade_start(channel, &params);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Effect error");
        return ESP_FAIL;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "effect", "fade");
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// POST /api/pwm/all-off - Turn all channels off
esp_err_t pwm_all_off_handler(httpd_req_t *req) {
    esp_err_t err = pca9685_all_off();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "PWM error");
        return ESP_FAIL;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "channels_affected", 16);
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// Register all PWM endpoints
void rest_api_register_pwm(httpd_handle_t server) {
    if (!server) return;
    
    static const httpd_uri_t uri_pwm_all_off = {
        .uri = "/api/pwm/all-off",
        .method = HTTP_POST,
        .handler = pwm_all_off_handler,
        .user_ctx = NULL
    };
    
    httpd_register_uri_handler(server, &uri_pwm_all_off);
    
    ESP_LOGI(TAG, "PWM API registered");
}
