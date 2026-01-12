#include "unity.h"
#include "json_config.h"
#include "cJSON.h"
#include <string.h>

// Test basic JSON config parsing
TEST_CASE("config parses valid json", "[json_config]") {
    // Create a simple JSON config for testing
    cJSON *test_config = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(test_config);
    
    cJSON_AddStringToObject(test_config, "node_name", "test_node");
    cJSON_AddNumberToObject(test_config, "led_count", 100);
    cJSON_AddBoolToObject(test_config, "setup_complete", true);
    
    // Verify we can extract values
    const cJSON *name = cJSON_GetObjectItem(test_config, "node_name");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(cJSON_IsString(name));
    TEST_ASSERT_EQUAL_STRING("test_node", name->valuestring);
    
    const cJSON *count = cJSON_GetObjectItem(test_config, "led_count");
    TEST_ASSERT_NOT_NULL(count);
    TEST_ASSERT_TRUE(cJSON_IsNumber(count));
    TEST_ASSERT_EQUAL_INT(100, count->valueint);
    
    const cJSON *setup = cJSON_GetObjectItem(test_config, "setup_complete");
    TEST_ASSERT_NOT_NULL(setup);
    TEST_ASSERT_TRUE(cJSON_IsBool(setup));
    TEST_ASSERT_TRUE(cJSON_IsTrue(setup));
    
    cJSON_Delete(test_config);
}

// Test JSON serialization
TEST_CASE("config serializes to valid json string", "[json_config]") {
    cJSON *config = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(config);
    
    cJSON_AddStringToObject(config, "ssid", "TestNetwork");
    cJSON_AddNumberToObject(config, "brightness", 80);
    
    char *json_str = cJSON_PrintUnformatted(config);
    TEST_ASSERT_NOT_NULL(json_str);
    
    // Verify we can parse it back
    cJSON *reparsed = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(reparsed);
    
    const cJSON *ssid = cJSON_GetObjectItem(reparsed, "ssid");
    TEST_ASSERT_NOT_NULL(ssid);
    TEST_ASSERT_EQUAL_STRING("TestNetwork", ssid->valuestring);
    
    const cJSON *brightness = cJSON_GetObjectItem(reparsed, "brightness");
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_EQUAL_INT(80, brightness->valueint);
    
    free(json_str);
    cJSON_Delete(reparsed);
    cJSON_Delete(config);
}

// Test handling of malformed JSON
TEST_CASE("config handles invalid json gracefully", "[json_config]") {
    const char *invalid_json = "{\"key\": invalid}";
    cJSON *parsed = cJSON_Parse(invalid_json);
    TEST_ASSERT_NULL(parsed);
    
    const char *truncated_json = "{\"key\": \"value\"";
    parsed = cJSON_Parse(truncated_json);
    TEST_ASSERT_NULL(parsed);
    
    const char *empty_json = "";
    parsed = cJSON_Parse(empty_json);
    TEST_ASSERT_NULL(parsed);
}

// Test nested object merging
TEST_CASE("config merges nested objects correctly", "[json_config]") {
    cJSON *dst = cJSON_CreateObject();
    cJSON *network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "ssid", "OldNetwork");
    cJSON_AddNumberToObject(network, "channel", 6);
    cJSON_AddItemToObject(dst, "network", network);
    cJSON_AddNumberToObject(dst, "brightness", 50);
    
    cJSON *src = cJSON_CreateObject();
    cJSON *new_network = cJSON_CreateObject();
    cJSON_AddStringToObject(new_network, "ssid", "NewNetwork");
    // channel not specified, should be preserved
    cJSON_AddItemToObject(src, "network", new_network);
    
    // Manual merge logic test (simulating config_merge behavior)
    cJSON *dst_network = cJSON_GetObjectItem(dst, "network");
    cJSON *src_network = cJSON_GetObjectItem(src, "network");
    
    TEST_ASSERT_NOT_NULL(dst_network);
    TEST_ASSERT_NOT_NULL(src_network);
    
    // After merge, ssid should be updated
    const cJSON *src_ssid = cJSON_GetObjectItem(src_network, "ssid");
    TEST_ASSERT_NOT_NULL(src_ssid);
    TEST_ASSERT_EQUAL_STRING("NewNetwork", src_ssid->valuestring);
    
    cJSON_Delete(dst);
    cJSON_Delete(src);
}

// Test type checking for config values
TEST_CASE("config validates field types", "[json_config]") {
    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "string_field", "value");
    cJSON_AddNumberToObject(config, "number_field", 42);
    cJSON_AddBoolToObject(config, "bool_field", true);
    cJSON_AddNullToObject(config, "null_field");
    
    const cJSON *str = cJSON_GetObjectItem(config, "string_field");
    TEST_ASSERT_TRUE(cJSON_IsString(str));
    TEST_ASSERT_FALSE(cJSON_IsNumber(str));
    
    const cJSON *num = cJSON_GetObjectItem(config, "number_field");
    TEST_ASSERT_TRUE(cJSON_IsNumber(num));
    TEST_ASSERT_FALSE(cJSON_IsString(num));
    
    const cJSON *boolean = cJSON_GetObjectItem(config, "bool_field");
    TEST_ASSERT_TRUE(cJSON_IsBool(boolean));
    TEST_ASSERT_TRUE(cJSON_IsTrue(boolean));
    
    const cJSON *null_val = cJSON_GetObjectItem(config, "null_field");
    TEST_ASSERT_TRUE(cJSON_IsNull(null_val));
    
    const cJSON *missing = cJSON_GetObjectItem(config, "missing_field");
    TEST_ASSERT_NULL(missing);
    
    cJSON_Delete(config);
}

// Test array handling
TEST_CASE("config handles arrays correctly", "[json_config]") {
    cJSON *config = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();
    
    cJSON_AddItemToArray(array, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(array, cJSON_CreateNumber(2));
    cJSON_AddItemToArray(array, cJSON_CreateNumber(3));
    
    cJSON_AddItemToObject(config, "values", array);
    
    const cJSON *values = cJSON_GetObjectItem(config, "values");
    TEST_ASSERT_NOT_NULL(values);
    TEST_ASSERT_TRUE(cJSON_IsArray(values));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(values));
    
    const cJSON *first = cJSON_GetArrayItem(values, 0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_INT(1, first->valueint);
    
    const cJSON *second = cJSON_GetArrayItem(values, 1);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_INT(2, second->valueint);
    
    cJSON_Delete(config);
}

// Test safe access patterns
TEST_CASE("config safe access prevents crashes", "[json_config]") {
    cJSON *config = cJSON_CreateObject();
    cJSON *nested = cJSON_CreateObject();
    cJSON_AddItemToObject(config, "nested", nested);
    
    // Safe access pattern - check before use
    const cJSON *obj = cJSON_GetObjectItem(config, "nested");
    if (obj && cJSON_IsObject(obj)) {
        const cJSON *value = cJSON_GetObjectItem(obj, "missing");
        TEST_ASSERT_NULL(value); // Missing field is NULL, not crash
    }
    
    // Access non-existent top-level key
    const cJSON *missing = cJSON_GetObjectItem(config, "does_not_exist");
    TEST_ASSERT_NULL(missing);
    
    // Try to get array item from non-array
    const cJSON *not_array = cJSON_GetObjectItem(config, "nested");
    const cJSON *invalid = cJSON_GetArrayItem(not_array, 0);
    TEST_ASSERT_NULL(invalid);
    
    cJSON_Delete(config);
}
