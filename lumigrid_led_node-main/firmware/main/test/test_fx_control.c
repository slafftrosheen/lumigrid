#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "esp_err.h"
#include "effect_control.h"
#include "effects.h"

static const effect_desc_t *s_last_fx = NULL;
static effect_params_t s_last_params;
static float s_last_brightness = -1.0f;
static bool s_params_set = false;
static bool s_brightness_set = false;

void effect_engine_set_fx_params(int ch, const effect_desc_t *fx, const effect_params_t *p)
{
    (void)ch;
    s_last_fx = fx;
    if (p) {
        s_last_params = *p;
        s_params_set = true;
    }
}

void effect_engine_set_brightness(int ch, float brightness)
{
    (void)ch;
    s_last_brightness = brightness;
    s_brightness_set = true;
}

TEST_CASE("fx_list enumerates registered descriptors", "[effect_control]")
{
    const char **names = NULL;
    size_t count = 0;
    esp_err_t err = fx_list(&names, &count);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(names);
    TEST_ASSERT_TRUE(count > 0);

    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT_NOT_NULL(names[i]);
        if (strcmp(names[i], "rainbow_flow") == 0) {
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);

    free((void *)names);
}

TEST_CASE("fx_set_channel_by_name applies descriptor and params", "[effect_control]")
{
    s_last_fx = NULL;
    s_params_set = false;
    s_last_params = (effect_params_t){0};

    effect_params_t params = {
        .speed = 1.25f,
        .intensity = 0.75f,
        .color = 0x112233,
        .palette_id = 3,
    };

    esp_err_t err = fx_set_channel_by_name(2, "twinkle", &params);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(s_last_fx);
    TEST_ASSERT_EQUAL_STRING("twinkle", s_last_fx->name);
    TEST_ASSERT_TRUE(s_params_set);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, params.speed, s_last_params.speed);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, params.intensity, s_last_params.intensity);
    TEST_ASSERT_EQUAL_UINT32(params.color, s_last_params.color);
    TEST_ASSERT_EQUAL_UINT8(params.palette_id, s_last_params.palette_id);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, fx_set_channel_by_name(0, "nope", NULL));
}

TEST_CASE("fx_set_channel_by_name falls back to defaults", "[effect_control]")
{
    s_last_fx = NULL;
    s_params_set = false;
    s_last_params = (effect_params_t){0};

    esp_err_t err = fx_set_channel_by_name(1, "meteor", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(s_last_fx);
    TEST_ASSERT_EQUAL_STRING("meteor", s_last_fx->name);
    TEST_ASSERT_TRUE(s_params_set);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, s_last_params.speed);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, s_last_params.intensity);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFu, s_last_params.color);
    TEST_ASSERT_EQUAL_UINT8(0, s_last_params.palette_id);
}

TEST_CASE("fx_set_channel_brightness clamps to range", "[effect_control]")
{
    s_last_brightness = -1.0f;
    s_brightness_set = false;

    fx_set_channel_brightness(1, 1.5f);
    TEST_ASSERT_TRUE(s_brightness_set);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, s_last_brightness);

    s_brightness_set = false;
    fx_set_channel_brightness(1, -0.5f);
    TEST_ASSERT_TRUE(s_brightness_set);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s_last_brightness);
}
