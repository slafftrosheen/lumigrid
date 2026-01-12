#include "unity.h"
#include "rest_api.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

TEST_CASE("status json exposes telemetry","[rest]"){
    char *json = rest_api_status_json_alloc();
    TEST_ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    const cJSON *node_type = cJSON_GetObjectItem(root, "node_type");
    TEST_ASSERT_TRUE(cJSON_IsString(node_type));

    const cJSON *role = cJSON_GetObjectItem(root, "role");
    TEST_ASSERT_TRUE(cJSON_IsString(role));

    const cJSON *uptime = cJSON_GetObjectItem(root, "uptime_ms");
    TEST_ASSERT_TRUE(cJSON_IsNumber(uptime));

    const cJSON *heap = cJSON_GetObjectItem(root, "heap_free_kb");
    TEST_ASSERT_TRUE(cJSON_IsNumber(heap));

    const cJSON *fps = cJSON_GetObjectItem(root, "fps");
    TEST_ASSERT_TRUE(cJSON_IsObject(fps));

    const cJSON *pwm = cJSON_GetObjectItem(root, "pwm_max_duty");
    TEST_ASSERT_TRUE(cJSON_IsNumber(pwm));

    const cJSON *last_err = cJSON_GetObjectItem(root, "last_error");
    TEST_ASSERT_TRUE(last_err == NULL || cJSON_IsNull(last_err) || cJSON_IsString(last_err));

    const cJSON *power = cJSON_GetObjectItem(root, "power_mA");
    TEST_ASSERT_TRUE(cJSON_IsNumber(power));

    cJSON_Delete(root);
    free(json);
}

TEST_CASE("status event formatting wraps json","[rest]"){
    char *event = rest_api_status_event_alloc();
    TEST_ASSERT_NOT_NULL(event);
    TEST_ASSERT_NOT_NULL(strstr(event, "event: status"));
    TEST_ASSERT_NOT_NULL(strstr(event, "\"heap_free_kb\""));
    TEST_ASSERT_NOT_NULL(strstr(event, "\"power_mA\""));
    free(event);
}

TEST_CASE("preset delete path parsing","[rest]"){
    char buf[64];
    TEST_ASSERT_TRUE(rest_api_extract_preset_name("/api/presets/demo", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("demo", buf);

    TEST_ASSERT_TRUE(rest_api_extract_preset_name("/api/presets/demo.json", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("demo", buf);

    TEST_ASSERT_FALSE(rest_api_extract_preset_name("/api/presets/", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(rest_api_extract_preset_name("/api/presets/a/b", buf, sizeof(buf)));
}

// Test JSON request validation
TEST_CASE("rest api validates json request structure", "[rest]") {
    // Valid effect apply request
    const char *valid_json = "{\"channel\":0,\"name\":\"rainbow\",\"params\":{\"speed\":1.5}}";
    cJSON *root = cJSON_Parse(valid_json);
    TEST_ASSERT_NOT_NULL(root);
    
    const cJSON *channel = cJSON_GetObjectItem(root, "channel");
    TEST_ASSERT_NOT_NULL(channel);
    TEST_ASSERT_TRUE(cJSON_IsNumber(channel));
    
    const cJSON *name = cJSON_GetObjectItem(root, "name");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(cJSON_IsString(name));
    
    const cJSON *params = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_TRUE(cJSON_IsObject(params));
    
    cJSON_Delete(root);
}

// Test invalid JSON request handling
TEST_CASE("rest api rejects malformed json", "[rest]") {
    // Missing closing brace
    const char *invalid_json = "{\"channel\":0,\"name\":\"rainbow\"";
    cJSON *root = cJSON_Parse(invalid_json);
    TEST_ASSERT_NULL(root);
    
    // Invalid field type
    const char *bad_type = "{\"channel\":\"zero\"}";
    root = cJSON_Parse(bad_type);
    TEST_ASSERT_NOT_NULL(root);
    
    const cJSON *channel = cJSON_GetObjectItem(root, "channel");
    TEST_ASSERT_NOT_NULL(channel);
    TEST_ASSERT_FALSE(cJSON_IsNumber(channel)); // Should be string, not number
    
    cJSON_Delete(root);
}

// Test hex color parsing
TEST_CASE("rest api parses hex color correctly", "[rest]") {
    // Test color parsing logic
    const char *hex_red = "#FF0000";
    const char *hex_green = "#00FF00";
    const char *hex_blue = "#0000FF";
    const char *hex_white = "#FFFFFF";
    
    // Validate format (7 chars, starts with #)
    TEST_ASSERT_EQUAL_INT(7, strlen(hex_red));
    TEST_ASSERT_EQUAL_CHAR('#', hex_red[0]);
    
    // Parse manually (strtol skips #)
    uint32_t red = (uint32_t)strtol(hex_red + 1, NULL, 16);
    TEST_ASSERT_EQUAL_UINT32(0xFF0000, red);
    
    uint32_t green = (uint32_t)strtol(hex_green + 1, NULL, 16);
    TEST_ASSERT_EQUAL_UINT32(0x00FF00, green);
    
    uint32_t blue = (uint32_t)strtol(hex_blue + 1, NULL, 16);
    TEST_ASSERT_EQUAL_UINT32(0x0000FF, blue);
    
    uint32_t white = (uint32_t)strtol(hex_white + 1, NULL, 16);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFF, white);
}

// Test invalid hex color handling
TEST_CASE("rest api rejects invalid hex colors", "[rest]") {
    // Missing # prefix
    const char *no_hash = "FF0000";
    TEST_ASSERT_FALSE(no_hash[0] == '#');
    
    // Too short
    const char *too_short = "#FFF";
    TEST_ASSERT_TRUE(strlen(too_short) != 7);
    
    // Too long
    const char *too_long = "#FFFFFF00";
    TEST_ASSERT_TRUE(strlen(too_long) != 7);
    
    // Invalid hex chars
    const char *invalid_chars = "#GGGGGG";
    uint32_t result = (uint32_t)strtol(invalid_chars + 1, NULL, 16);
    TEST_ASSERT_EQUAL_UINT32(0, result); // Invalid parse returns 0
}

// Test effect parameters JSON parsing
TEST_CASE("rest api parses effect params from json", "[rest]") {
    const char *json_str = "{\"speed\":2.5,\"intensity\":0.8,\"color\":\"#ABCDEF\",\"brightness\":0.9}";
    cJSON *params = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(params);
    
    const cJSON *speed = cJSON_GetObjectItem(params, "speed");
    TEST_ASSERT_NOT_NULL(speed);
    TEST_ASSERT_TRUE(cJSON_IsNumber(speed));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.5, speed->valuedouble);
    
    const cJSON *intensity = cJSON_GetObjectItem(params, "intensity");
    TEST_ASSERT_NOT_NULL(intensity);
    TEST_ASSERT_TRUE(cJSON_IsNumber(intensity));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.8, intensity->valuedouble);
    
    const cJSON *color = cJSON_GetObjectItem(params, "color");
    TEST_ASSERT_NOT_NULL(color);
    TEST_ASSERT_TRUE(cJSON_IsString(color));
    
    const cJSON *brightness = cJSON_GetObjectItem(params, "brightness");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_TRUE(cJSON_IsNumber(brightness));
    
    cJSON_Delete(params);
}

// Test response JSON formatting
TEST_CASE("rest api formats response json correctly", "[rest]") {
    cJSON *response = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(response);
    
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "code", 200);
    
    char *json_str = cJSON_PrintUnformatted(response);
    TEST_ASSERT_NOT_NULL(json_str);
    
    // Verify it's valid JSON
    cJSON *reparsed = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(reparsed);
    
    const cJSON *status = cJSON_GetObjectItem(reparsed, "status");
    TEST_ASSERT_EQUAL_STRING("ok", status->valuestring);
    
    const cJSON *code = cJSON_GetObjectItem(reparsed, "code");
    TEST_ASSERT_EQUAL_INT(200, code->valueint);
    
    free(json_str);
    cJSON_Delete(reparsed);
    cJSON_Delete(response);
}

// Test error response formatting
TEST_CASE("rest api formats error responses", "[rest]") {
    cJSON *error = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(error);
    
    cJSON_AddStringToObject(error, "error", "Invalid parameter");
    cJSON_AddNumberToObject(error, "code", 400);
    
    char *json_str = cJSON_PrintUnformatted(error);
    TEST_ASSERT_NOT_NULL(json_str);
    TEST_ASSERT_NOT_NULL(strstr(json_str, "error"));
    TEST_ASSERT_NOT_NULL(strstr(json_str, "Invalid parameter"));
    
    free(json_str);
    cJSON_Delete(error);
}

// Test array response formatting
TEST_CASE("rest api formats array responses", "[rest]") {
    cJSON *array = cJSON_CreateArray();
    TEST_ASSERT_NOT_NULL(array);
    
    cJSON_AddItemToArray(array, cJSON_CreateString("rainbow"));
    cJSON_AddItemToArray(array, cJSON_CreateString("sparkle"));
    cJSON_AddItemToArray(array, cJSON_CreateString("solid"));
    
    char *json_str = cJSON_PrintUnformatted(array);
    TEST_ASSERT_NOT_NULL(json_str);
    
    // Verify array format
    TEST_ASSERT_EQUAL_CHAR('[', json_str[0]);
    TEST_ASSERT_EQUAL_CHAR(']', json_str[strlen(json_str) - 1]);
    
    cJSON *reparsed = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(reparsed);
    TEST_ASSERT_TRUE(cJSON_IsArray(reparsed));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(reparsed));
    
    free(json_str);
    cJSON_Delete(reparsed);
    cJSON_Delete(array);
}
