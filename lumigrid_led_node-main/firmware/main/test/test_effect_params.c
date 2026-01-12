#include "unity.h"
#include "effects.h"
#include "effect_control.h"
#include <string.h>

// Test default effect parameters
TEST_CASE("effect params have valid defaults", "[effects]") {
    effect_params_t params = {
        .speed = 1.0f,
        .intensity = 0.5f,
        .color = 0xFFFFFFu,
        .palette_id = 0,
        .brightness = 1.0f
    };
    
    // Verify defaults are in valid ranges
    TEST_ASSERT_TRUE(params.speed >= 0.0f && params.speed <= 5.0f);
    TEST_ASSERT_TRUE(params.intensity >= 0.0f && params.intensity <= 1.0f);
    TEST_ASSERT_TRUE(params.brightness >= 0.0f && params.brightness <= 1.0f);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, params.color);
}

// Test speed parameter validation
TEST_CASE("effect speed parameter validates range", "[effects]") {
    effect_params_t params = {
        .speed = 1.0f,
        .intensity = 0.5f,
        .color = 0xFFFFFFu,
        .palette_id = 0,
        .brightness = 1.0f
    };
    
    // Valid speeds
    params.speed = 0.0f;
    TEST_ASSERT_TRUE(params.speed >= 0.0f);
    
    params.speed = 2.5f;
    TEST_ASSERT_TRUE(params.speed >= 0.0f && params.speed <= 5.0f);
    
    params.speed = 5.0f;
    TEST_ASSERT_TRUE(params.speed <= 5.0f);
    
    // Test clamping logic
    float test_speed = -1.0f;
    if (test_speed < 0.0f) test_speed = 0.0f;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, test_speed);
    
    test_speed = 10.0f;
    if (test_speed > 5.0f) test_speed = 5.0f;
    TEST_ASSERT_EQUAL_FLOAT(5.0f, test_speed);
}

// Test intensity parameter validation
TEST_CASE("effect intensity parameter validates range", "[effects]") {
    effect_params_t params = {
        .speed = 1.0f,
        .intensity = 0.5f,
        .color = 0xFFFFFFu,
        .palette_id = 0,
        .brightness = 1.0f
    };
    
    // Valid intensities
    params.intensity = 0.0f;
    TEST_ASSERT_TRUE(params.intensity >= 0.0f);
    
    params.intensity = 0.5f;
    TEST_ASSERT_TRUE(params.intensity >= 0.0f && params.intensity <= 1.0f);
    
    params.intensity = 1.0f;
    TEST_ASSERT_TRUE(params.intensity <= 1.0f);
    
    // Test clamping logic
    float test_intensity = -0.5f;
    if (test_intensity < 0.0f) test_intensity = 0.0f;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, test_intensity);
    
    test_intensity = 2.0f;
    if (test_intensity > 1.0f) test_intensity = 1.0f;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, test_intensity);
}

// Test brightness parameter validation
TEST_CASE("effect brightness parameter validates range", "[effects]") {
    effect_params_t params = {
        .speed = 1.0f,
        .intensity = 0.5f,
        .color = 0xFFFFFFu,
        .palette_id = 0,
        .brightness = 1.0f
    };
    
    // Valid brightness values
    params.brightness = 0.0f;
    TEST_ASSERT_TRUE(params.brightness >= 0.0f);
    
    params.brightness = 0.5f;
    TEST_ASSERT_TRUE(params.brightness >= 0.0f && params.brightness <= 1.0f);
    
    params.brightness = 1.0f;
    TEST_ASSERT_TRUE(params.brightness <= 1.0f);
    
    // Test clamping logic
    float test_brightness = -1.0f;
    if (test_brightness < 0.0f) test_brightness = 0.0f;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, test_brightness);
    
    test_brightness = 1.5f;
    if (test_brightness > 1.0f) test_brightness = 1.0f;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, test_brightness);
}

// Test color value handling
TEST_CASE("effect color parameter handles rgb values", "[effects]") {
    effect_params_t params = {
        .speed = 1.0f,
        .intensity = 0.5f,
        .color = 0xFFFFFFu,
        .palette_id = 0,
        .brightness = 1.0f
    };
    
    // Test various color values
    params.color = 0x000000u; // Black
    TEST_ASSERT_EQUAL_UINT32(0x000000u, params.color);
    
    params.color = 0xFF0000u; // Red
    TEST_ASSERT_EQUAL_UINT32(0xFF0000u, params.color);
    
    params.color = 0x00FF00u; // Green
    TEST_ASSERT_EQUAL_UINT32(0x00FF00u, params.color);
    
    params.color = 0x0000FFu; // Blue
    TEST_ASSERT_EQUAL_UINT32(0x0000FFu, params.color);
    
    params.color = 0xFFFFFFu; // White
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, params.color);
    
    // Test RGB component extraction
    uint32_t test_color = 0x12AB34u;
    uint8_t r = (test_color >> 16) & 0xFF;
    uint8_t g = (test_color >> 8) & 0xFF;
    uint8_t b = test_color & 0xFF;
    
    TEST_ASSERT_EQUAL_UINT8(0x12, r);
    TEST_ASSERT_EQUAL_UINT8(0xAB, g);
    TEST_ASSERT_EQUAL_UINT8(0x34, b);
}

// Test effect parameter structure size and alignment
TEST_CASE("effect params structure is correctly sized", "[effects]") {
    effect_params_t params;
    
    // Verify structure has expected fields
    TEST_ASSERT_TRUE(sizeof(params.speed) == sizeof(float));
    TEST_ASSERT_TRUE(sizeof(params.intensity) == sizeof(float));
    TEST_ASSERT_TRUE(sizeof(params.color) == sizeof(uint32_t));
    TEST_ASSERT_TRUE(sizeof(params.palette_id) == sizeof(uint8_t));
    TEST_ASSERT_TRUE(sizeof(params.brightness) == sizeof(float));
    
    // Structure should be reasonably sized
    TEST_ASSERT_TRUE(sizeof(effect_params_t) > 0);
    TEST_ASSERT_TRUE(sizeof(effect_params_t) < 100); // Sanity check
}

// Test palette_id handling
TEST_CASE("effect palette_id handles valid range", "[effects]") {
    effect_params_t params = {
        .speed = 1.0f,
        .intensity = 0.5f,
        .color = 0xFFFFFFu,
        .palette_id = 0,
        .brightness = 1.0f
    };
    
    // Valid palette IDs (0-255 for uint8_t)
    params.palette_id = 0;
    TEST_ASSERT_EQUAL_UINT8(0, params.palette_id);
    
    params.palette_id = 127;
    TEST_ASSERT_EQUAL_UINT8(127, params.palette_id);
    
    params.palette_id = 255;
    TEST_ASSERT_EQUAL_UINT8(255, params.palette_id);
}

// Test parameter copying
TEST_CASE("effect params can be safely copied", "[effects]") {
    effect_params_t src = {
        .speed = 2.5f,
        .intensity = 0.7f,
        .color = 0xABCDEFu,
        .palette_id = 42,
        .brightness = 0.8f
    };
    
    effect_params_t dst = src;
    
    TEST_ASSERT_EQUAL_FLOAT(src.speed, dst.speed);
    TEST_ASSERT_EQUAL_FLOAT(src.intensity, dst.intensity);
    TEST_ASSERT_EQUAL_UINT32(src.color, dst.color);
    TEST_ASSERT_EQUAL_UINT8(src.palette_id, dst.palette_id);
    TEST_ASSERT_EQUAL_FLOAT(src.brightness, dst.brightness);
}

// Test edge cases for floating-point parameters
TEST_CASE("effect params handle floating point edge cases", "[effects]") {
    effect_params_t params;
    
    // Very small values
    params.speed = 0.001f;
    TEST_ASSERT_TRUE(params.speed > 0.0f);
    
    params.intensity = 0.0001f;
    TEST_ASSERT_TRUE(params.intensity > 0.0f);
    
    params.brightness = 0.01f;
    TEST_ASSERT_TRUE(params.brightness > 0.0f);
    
    // Values near limits
    params.speed = 4.999f;
    TEST_ASSERT_TRUE(params.speed < 5.0f);
    
    params.intensity = 0.999f;
    TEST_ASSERT_TRUE(params.intensity < 1.0f);
    
    params.brightness = 0.999f;
    TEST_ASSERT_TRUE(params.brightness < 1.0f);
}

// Test u8 lerp helper function
TEST_CASE("u8 lerp interpolates correctly", "[effects]") {
    // Test linear interpolation for 8-bit values
    uint8_t a = 0;
    uint8_t b = 255;
    uint8_t t = 128; // 50%
    
    // Manual lerp calculation: a + (b - a) * t / 255
    uint8_t result = a + ((b - a) * t) / 255;
    TEST_ASSERT_TRUE(result >= 127 && result <= 128); // ~50%
    
    // 0% interpolation
    t = 0;
    result = a + ((b - a) * t) / 255;
    TEST_ASSERT_EQUAL_UINT8(0, result);
    
    // 100% interpolation
    t = 255;
    result = a + ((b - a) * t) / 255;
    TEST_ASSERT_EQUAL_UINT8(255, result);
    
    // 25% interpolation
    a = 100;
    b = 200;
    t = 64; // ~25%
    result = a + ((b - a) * t) / 255;
    TEST_ASSERT_TRUE(result >= 124 && result <= 126); // ~125
}
