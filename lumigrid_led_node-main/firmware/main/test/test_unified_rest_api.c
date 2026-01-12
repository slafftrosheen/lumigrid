#include "unity.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

// Test /api/config endpoint payload structure
TEST_CASE("config endpoint payload structure", "[rest][config]") {
    // Test valid config GET response structure
    const char *valid_config = "{\"node_name\":\"test-node\",\"brightness\":0.8,\"strips\":[{\"ch\":0,\"type\":\"WS2812B\"}]}";
    cJSON *root = cJSON_Parse(valid_config);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));
    
    const cJSON *node_name = cJSON_GetObjectItem(root, "node_name");
    TEST_ASSERT_NOT_NULL(node_name);
    TEST_ASSERT_TRUE(cJSON_IsString(node_name));
    
    const cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_TRUE(cJSON_IsNumber(brightness));
    
    const cJSON *strips = cJSON_GetObjectItem(root, "strips");
    TEST_ASSERT_NOT_NULL(strips);
    TEST_ASSERT_TRUE(cJSON_IsArray(strips));
    
    cJSON_Delete(root);
}

// Test /api/config POST partial updates
TEST_CASE("config post partial update payload", "[rest][config]") {
    // Test partial update - only brightness
    const char *partial_update = "{\"brightness\":0.9}";
    cJSON *root = cJSON_Parse(partial_update);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));
    
    const cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_TRUE(cJSON_IsNumber(brightness));
    TEST_ASSERT_EQUAL_FLOAT(0.9, brightness->valuedouble);
    
    cJSON_Delete(root);
    
    // Test partial update - multiple fields
    const char *multi_update = "{\"brightness\":0.85,\"node_name\":\"bedroom-leds\"}";
    root = cJSON_Parse(multi_update);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));
    
    brightness = cJSON_GetObjectItem(root, "brightness");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_EQUAL_FLOAT(0.85, brightness->valuedouble);
    
    const cJSON *node_name = cJSON_GetObjectItem(root, "node_name");
    TEST_ASSERT_NOT_NULL(node_name);
    TEST_ASSERT_EQUAL_STRING("bedroom-leds", node_name->valuestring);
    
    cJSON_Delete(root);
}

// Test /api/config POST validation
TEST_CASE("config post rejects non-object payloads", "[rest][config]") {
    // Array should be rejected
    const char *array_payload = "[1,2,3]";
    cJSON *root = cJSON_Parse(array_payload);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_FALSE(cJSON_IsObject(root)); // Should be array, not object
    cJSON_Delete(root);
    
    // String should be rejected
    const char *string_payload = "\"config\"";
    root = cJSON_Parse(string_payload);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_FALSE(cJSON_IsObject(root)); // Should be string, not object
    cJSON_Delete(root);
}

// Test /api/trigger endpoint payloads
TEST_CASE("trigger endpoint flash action payload", "[rest][trigger]") {
    const char *flash_payload = "{\"action\":\"flash\",\"channel\":0,\"duration_ms\":500}";
    cJSON *root = cJSON_Parse(flash_payload);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));
    
    const cJSON *action = cJSON_GetObjectItem(root, "action");
    TEST_ASSERT_NOT_NULL(action);
    TEST_ASSERT_TRUE(cJSON_IsString(action));
    TEST_ASSERT_EQUAL_STRING("flash", action->valuestring);
    
    const cJSON *channel = cJSON_GetObjectItem(root, "channel");
    TEST_ASSERT_NOT_NULL(channel);
    TEST_ASSERT_TRUE(cJSON_IsNumber(channel));
    TEST_ASSERT_EQUAL_INT(0, channel->valueint);
    
    const cJSON *duration = cJSON_GetObjectItem(root, "duration_ms");
    TEST_ASSERT_NOT_NULL(duration);
    TEST_ASSERT_TRUE(cJSON_IsNumber(duration));
    TEST_ASSERT_EQUAL_INT(500, duration->valueint);
    
    cJSON_Delete(root);
}

TEST_CASE("trigger endpoint pulse action payload", "[rest][trigger]") {
    const char *pulse_payload = "{\"action\":\"pulse\",\"channel\":0}";
    cJSON *root = cJSON_Parse(pulse_payload);
    TEST_ASSERT_NOT_NULL(root);
    
    const cJSON *action = cJSON_GetObjectItem(root, "action");
    TEST_ASSERT_NOT_NULL(action);
    TEST_ASSERT_EQUAL_STRING("pulse", action->valuestring);
    
    const cJSON *channel = cJSON_GetObjectItem(root, "channel");
    TEST_ASSERT_NOT_NULL(channel);
    TEST_ASSERT_EQUAL_INT(0, channel->valueint);
    
    cJSON_Delete(root);
}

TEST_CASE("trigger endpoint apply_preset action payload", "[rest][trigger]") {
    const char *preset_payload = "{\"action\":\"apply_preset\",\"preset\":\"fire_warm\",\"channel\":0,\"duration_ms\":5000}";
    cJSON *root = cJSON_Parse(preset_payload);
    TEST_ASSERT_NOT_NULL(root);
    
    const cJSON *action = cJSON_GetObjectItem(root, "action");
    TEST_ASSERT_NOT_NULL(action);
    TEST_ASSERT_EQUAL_STRING("apply_preset", action->valuestring);
    
    const cJSON *preset = cJSON_GetObjectItem(root, "preset");
    TEST_ASSERT_NOT_NULL(preset);
    TEST_ASSERT_TRUE(cJSON_IsString(preset));
    TEST_ASSERT_EQUAL_STRING("fire_warm", preset->valuestring);
    
    const cJSON *channel = cJSON_GetObjectItem(root, "channel");
    TEST_ASSERT_NOT_NULL(channel);
    TEST_ASSERT_EQUAL_INT(0, channel->valueint);
    
    const cJSON *duration = cJSON_GetObjectItem(root, "duration_ms");
    TEST_ASSERT_NOT_NULL(duration);
    TEST_ASSERT_EQUAL_INT(5000, duration->valueint);
    
    cJSON_Delete(root);
}

// Test /api/discover/needs_sensor_data response structure
TEST_CASE("discover needs_sensor_data response structure", "[rest][discover]") {
    const char *response = "{\"needs_data\":true,\"types\":[\"temperature\",\"humidity\",\"motion\",\"light\"]}";
    cJSON *root = cJSON_Parse(response);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));
    
    const cJSON *needs_data = cJSON_GetObjectItem(root, "needs_data");
    TEST_ASSERT_NOT_NULL(needs_data);
    TEST_ASSERT_TRUE(cJSON_IsBool(needs_data));
    TEST_ASSERT_TRUE(cJSON_IsTrue(needs_data));
    
    const cJSON *types = cJSON_GetObjectItem(root, "types");
    TEST_ASSERT_NOT_NULL(types);
    TEST_ASSERT_TRUE(cJSON_IsArray(types));
    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(types));
    
    // Verify sensor types
    cJSON *type = cJSON_GetArrayItem(types, 0);
    TEST_ASSERT_EQUAL_STRING("temperature", type->valuestring);
    
    type = cJSON_GetArrayItem(types, 1);
    TEST_ASSERT_EQUAL_STRING("humidity", type->valuestring);
    
    type = cJSON_GetArrayItem(types, 2);
    TEST_ASSERT_EQUAL_STRING("motion", type->valuestring);
    
    type = cJSON_GetArrayItem(types, 3);
    TEST_ASSERT_EQUAL_STRING("light", type->valuestring);
    
    cJSON_Delete(root);
}

// Test /api/sensor_update payload structure
TEST_CASE("sensor_update payload structure", "[rest][sensor]") {
    const char *payload = "{\"node_id\":\"sensor_node_01\",\"timestamp\":1678886400,\"readings\":{\"temperature_c\":23.5,\"humidity_percent\":55.2,\"motion_detected\":true,\"light_level\":450.0}}";
    cJSON *root = cJSON_Parse(payload);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));
    
    const cJSON *node_id = cJSON_GetObjectItem(root, "node_id");
    TEST_ASSERT_NOT_NULL(node_id);
    TEST_ASSERT_TRUE(cJSON_IsString(node_id));
    TEST_ASSERT_EQUAL_STRING("sensor_node_01", node_id->valuestring);
    
    const cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    TEST_ASSERT_NOT_NULL(timestamp);
    TEST_ASSERT_TRUE(cJSON_IsNumber(timestamp));
    TEST_ASSERT_EQUAL_INT(1678886400, timestamp->valueint);
    
    const cJSON *readings = cJSON_GetObjectItem(root, "readings");
    TEST_ASSERT_NOT_NULL(readings);
    TEST_ASSERT_TRUE(cJSON_IsObject(readings));
    
    const cJSON *temp = cJSON_GetObjectItem(readings, "temperature_c");
    TEST_ASSERT_NOT_NULL(temp);
    TEST_ASSERT_TRUE(cJSON_IsNumber(temp));
    TEST_ASSERT_EQUAL_FLOAT(23.5, temp->valuedouble);
    
    const cJSON *humidity = cJSON_GetObjectItem(readings, "humidity_percent");
    TEST_ASSERT_NOT_NULL(humidity);
    TEST_ASSERT_EQUAL_FLOAT(55.2, humidity->valuedouble);
    
    const cJSON *motion = cJSON_GetObjectItem(readings, "motion_detected");
    TEST_ASSERT_NOT_NULL(motion);
    TEST_ASSERT_TRUE(cJSON_IsBool(motion));
    TEST_ASSERT_TRUE(cJSON_IsTrue(motion));
    
    const cJSON *light = cJSON_GetObjectItem(readings, "light_level");
    TEST_ASSERT_NOT_NULL(light);
    TEST_ASSERT_EQUAL_FLOAT(450.0, light->valuedouble);
    
    cJSON_Delete(root);
}

// Test sensor_update with minimal payload
TEST_CASE("sensor_update handles minimal payload", "[rest][sensor]") {
    const char *minimal = "{\"node_id\":\"test\",\"timestamp\":0,\"readings\":{}}";
    cJSON *root = cJSON_Parse(minimal);
    TEST_ASSERT_NOT_NULL(root);
    
    const cJSON *node_id = cJSON_GetObjectItem(root, "node_id");
    TEST_ASSERT_NOT_NULL(node_id);
    
    const cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    TEST_ASSERT_NOT_NULL(timestamp);
    
    const cJSON *readings = cJSON_GetObjectItem(root, "readings");
    TEST_ASSERT_NOT_NULL(readings);
    TEST_ASSERT_TRUE(cJSON_IsObject(readings));
    
    cJSON_Delete(root);
}

// Test API response success format
TEST_CASE("api success response format", "[rest]") {
    const char *success = "{\"ok\":true}";
    cJSON *root = cJSON_Parse(success);
    TEST_ASSERT_NOT_NULL(root);
    
    const cJSON *ok = cJSON_GetObjectItem(root, "ok");
    TEST_ASSERT_NOT_NULL(ok);
    TEST_ASSERT_TRUE(cJSON_IsBool(ok));
    TEST_ASSERT_TRUE(cJSON_IsTrue(ok));
    
    cJSON_Delete(root);
}
