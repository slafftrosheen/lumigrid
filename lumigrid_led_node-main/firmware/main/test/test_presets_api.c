#include "unity.h"
#include "storage_fs.h"
#include "rest_api.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

// Schema constants (from preset-led.schema.json)
#define PRESET_NAME_MAX_LENGTH 64
#define PRESET_DESCRIPTION_MAX_LENGTH 256

static const char *TEST_PRESET_JSON = "{\"name\":\"test_delete\",\"value\":123}";

TEST_CASE("preset path write/read basic","[rest][preset]"){
    // quick in-process check of atomic write pattern
    char base[128];
    TEST_ASSERT_TRUE(storage_fs_mount_path(base, sizeof(base), "presets"));
    size_t base_len = strlen(base);
    TEST_ASSERT_TRUE(base_len + strlen("/_test.json") < sizeof(base));
    strcpy(base + base_len, "/_test.json");
    const char *p = base;
    const char *txt = "{\"ok\":1}";
    // emulate save
    char tmp[160]; snprintf(tmp,sizeof(tmp),"%s.tmp",p);
    FILE *f=fopen(tmp,"w"); TEST_ASSERT_NOT_NULL(f);
    size_t w=fwrite(txt,1,strlen(txt),f); fclose(f);
    TEST_ASSERT_EQUAL(strlen(txt), w);
    TEST_ASSERT_EQUAL(0, rename(tmp,p));
    // read-back
    uint8_t *buf=NULL; size_t L=0; TEST_ASSERT_EQUAL(ESP_OK, storage_fs_read(p,&buf,&L));
    TEST_ASSERT_EQUAL(strlen(txt), L);
    free(buf);
    // cleanup
    unlink(p);
}

TEST_CASE("preset extract name from URI", "[rest][preset]"){
    char name[64];
    
    // Valid cases
    TEST_ASSERT_TRUE(rest_api_extract_preset_name("/api/presets/mypreset", name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("mypreset", name);
    
    TEST_ASSERT_TRUE(rest_api_extract_preset_name("/api/presets/test123", name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("test123", name);
    
    TEST_ASSERT_TRUE(rest_api_extract_preset_name("/api/presets/my-preset", name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("my-preset", name);
    
    // With .json extension - should be stripped
    TEST_ASSERT_TRUE(rest_api_extract_preset_name("/api/presets/mypreset.json", name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("mypreset", name);
    
    // Invalid cases
    TEST_ASSERT_FALSE(rest_api_extract_preset_name("/api/presets/", name, sizeof(name)));
    TEST_ASSERT_FALSE(rest_api_extract_preset_name("/api/presets", name, sizeof(name)));
    TEST_ASSERT_FALSE(rest_api_extract_preset_name("/api/other/path", name, sizeof(name)));
    TEST_ASSERT_FALSE(rest_api_extract_preset_name("/api/presets/sub/path", name, sizeof(name)));
}

TEST_CASE("preset delete operation", "[rest][preset]"){
    // Setup - create a test preset
    char path[160];
    TEST_ASSERT_TRUE(storage_fs_mount_path(path, sizeof(path), "presets"));
    size_t base_len = strlen(path);
    snprintf(path + base_len, sizeof(path) - base_len, "/test_delete.json");
    
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fwrite(TEST_PRESET_JSON, 1, strlen(TEST_PRESET_JSON), f);
    fclose(f);
    
    // Verify it exists
    f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(f);
    fclose(f);
    
    // Delete it
    esp_err_t err = rest_api_delete_preset("test_delete");
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Verify it's gone
    f = fopen(path, "r");
    TEST_ASSERT_NULL(f);
    
    // Delete again should fail
    err = rest_api_delete_preset("test_delete");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

TEST_CASE("preset validation - valid minimal preset", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test_fx\",\"effect_id\":1101}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    if (!valid) {
        printf("Validation error: %s\n", error);
    }
    TEST_ASSERT_TRUE(valid);
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - valid full preset with colors", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{"
        "\"preset_name\":\"led_ocean\","
        "\"description\":\"Ocean wave effect\","
        "\"effect_id\":1105,"
        "\"speed\":0.8,"
        "\"intensity\":1.0,"
        "\"palette_id\":1,"
        "\"color1\":[0,0,255],"
        "\"color2\":[0,100,200],"
        "\"color3\":[0,50,150]"
        "}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    if (!valid) {
        printf("Validation error: %s\n", error);
    }
    TEST_ASSERT_TRUE(valid);
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - missing preset_name", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"effect_id\":1101}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "preset_name"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - missing effect_id", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\"}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "effect_id"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - effect_id out of range", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\",\"effect_id\":500}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "out of range"));
    TEST_ASSERT_NOT_NULL(strstr(error, "1000"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - speed out of range", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\",\"effect_id\":1101,\"speed\":10.0}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "speed"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - intensity out of range", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\",\"effect_id\":1101,\"intensity\":1.5}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "intensity"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - invalid color format (object)", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\",\"effect_id\":1101,\"color1\":{\"r\":255,\"g\":0,\"b\":0}}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "color"));
    TEST_ASSERT_NOT_NULL(strstr(error, "array"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - color array wrong size", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\",\"effect_id\":1101,\"color1\":[255,0]}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "3 elements"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - color value out of range", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test\",\"effect_id\":1101,\"color1\":[255,0,300]}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "out of range"));
    TEST_ASSERT_NOT_NULL(strstr(error, "255"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - preset_name too long", "[rest][preset][validation]") {
    char error[256];
    // Create a name that exceeds PRESET_NAME_MAX_LENGTH
    char long_name[PRESET_NAME_MAX_LENGTH + 10];
    memset(long_name, 'a', PRESET_NAME_MAX_LENGTH + 1);
    long_name[PRESET_NAME_MAX_LENGTH + 1] = '\0';
    
    char json[512];
    snprintf(json, sizeof(json), "{\"preset_name\":\"%s\",\"effect_id\":1101}", long_name);
    
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "too long"));
    
    cJSON_Delete(root);
}

TEST_CASE("preset validation - invalid preset_name characters", "[rest][preset][validation]") {
    char error[256];
    const char *json = "{\"preset_name\":\"test/path\",\"effect_id\":1101}";
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    
    bool valid = rest_api_validate_preset_json(root, error, sizeof(error));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_NOT_NULL(strstr(error, "invalid characters"));
    
    cJSON_Delete(root);
}

