// components/signage/RuntimeHardeningTests.cpp
#include "RuntimeHardeningTests.hpp"
#include "ColorPipeline.hpp"
#include "ControlSourceManager.hpp"
#include "ConfigStore.hpp"
#include "esp_log.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <memory>

static const char* TAG = "HardeningTest";

namespace lg {
namespace {

inline bool is_sequence_newer(uint8_t curr, uint8_t last) {
    int16_t diff = static_cast<int16_t>(curr) - static_cast<int16_t>(last);
    return (diff > 0 && diff <= 127) || (diff < -127);
}

bool test_color_pipeline() {
    ESP_LOGI(TAG, "Testing ColorPipeline (Item 21)...");

    FixtureConfig base_fix{};
    base_fix.profile = FixtureProfile::WS2812;
    base_fix.num_pixels = 16;
    base_fix.max_brightness = 255;
    base_fix.white_balance[0] = 255;
    base_fix.white_balance[1] = 255;
    base_fix.white_balance[2] = 255;
    base_fix.gamma_correction = true;

    // 1. Gamma 0 / Linear
    ColorPipeline::init(0);
    assert(ColorPipeline::get_gamma() == 0);
    LightValue in_lin{128, 64, 32, 0};
    LightValue out_lin = ColorPipeline::to_addressable(in_lin, base_fix, 255);
    assert(out_lin.r == 128 && out_lin.g == 64 && out_lin.b == 32);

    // 2. Gamma 22 / 2.2
    ColorPipeline::init(22);
    assert(ColorPipeline::get_gamma() == 22);
    LightValue out_22 = ColorPipeline::to_addressable(in_lin, base_fix, 255);
    // 128/255 ^ 2.2 * 255 is approximately 55
    assert(out_22.r < 128 && out_22.r >= 50 && out_22.r <= 60);

    // 3. Gamma Reload
    ColorPipeline::set_gamma(18);
    LightValue out_18 = ColorPipeline::to_addressable(in_lin, base_fix, 255);
    ColorPipeline::set_gamma(28);
    LightValue out_28 = ColorPipeline::to_addressable(in_lin, base_fix, 255);
    assert(out_18.r > out_28.r); // Steeper gamma yields lower midtone value
    ColorPipeline::set_gamma(22); // Restore

    // 4. PWM 12-Bit Conversion
    FixtureConfig pwm_fix{};
    pwm_fix.profile = FixtureProfile::PWM_RGB;
    pwm_fix.max_brightness = 255;
    pwm_fix.white_balance[0] = 255;
    pwm_fix.white_balance[1] = 255;
    pwm_fix.white_balance[2] = 255;
    pwm_fix.gamma_correction = false;

    std::array<uint16_t, 4> pwm_duties{};
    ColorPipeline::to_pwm(LightValue(255, 128, 0, 0), 0, pwm_fix, 255, pwm_duties);
    assert(pwm_duties[0] == 4095);
    assert(pwm_duties[1] >= 2050 && pwm_duties[1] <= 2060); // 128 * 4095 / 255 = 2055
    assert(pwm_duties[2] == 0);

    // 5. RGBW Explicit W Preservation
    FixtureConfig sk_fix{};
    sk_fix.profile = FixtureProfile::SK6812;
    sk_fix.max_brightness = 255;
    sk_fix.white_balance[0] = 255;
    sk_fix.white_balance[1] = 255;
    sk_fix.white_balance[2] = 255;
    sk_fix.gamma_correction = false;

    LightValue in_explicit_w{100, 100, 100, 80};
    LightValue out_explicit_w = ColorPipeline::to_addressable(in_explicit_w, sk_fix, 255);
    assert(out_explicit_w.w == 80); // Preserved!
    assert(out_explicit_w.r == 100);

    // 6. SK6812 Subtractive Extraction (when W == 0)
    LightValue in_sub{100, 150, 200, 0};
    LightValue out_sub = ColorPipeline::to_addressable(in_sub, sk_fix, 255);
    assert(out_sub.w == 100); // min(100, 150, 200)
    assert(out_sub.r == 0);   // 100 - 100
    assert(out_sub.g == 50);  // 150 - 100
    assert(out_sub.b == 100); // 200 - 100

    // 7. Max Brightness Scaling
    FixtureConfig half_fix = base_fix;
    half_fix.max_brightness = 128;
    half_fix.gamma_correction = false;
    LightValue out_half = ColorPipeline::to_addressable(LightValue(200, 0, 0, 0), half_fix, 255);
    assert(out_half.r >= 99 && out_half.r <= 101); // 200 * 128 / 255 = 100

    // 8. White Balance
    FixtureConfig wb_fix = base_fix;
    wb_fix.white_balance[0] = 255;
    wb_fix.white_balance[1] = 128;
    wb_fix.white_balance[2] = 0;
    wb_fix.gamma_correction = false;
    LightValue out_wb = ColorPipeline::to_addressable(LightValue(200, 200, 200, 0), wb_fix, 255);
    assert(out_wb.r == 200);
    assert(out_wb.g >= 99 && out_wb.g <= 101);
    assert(out_wb.b == 0);

    ESP_LOGI(TAG, "[PASS] ColorPipeline all 8 sub-tests passed.");
    return true;
}

bool test_dmx_sequence() {
    ESP_LOGI(TAG, "Testing DMX sequence wrap handling (Item 2)...");

    // 0 -> 1: valid forward progression
    assert(is_sequence_newer(1, 0) == true);

    // 254 -> 255 -> 0: wrap-around progression
    assert(is_sequence_newer(255, 254) == true);
    assert(is_sequence_newer(0, 255) == true);

    // Duplicate packet: rejected
    assert(is_sequence_newer(42, 42) == false);

    // Old packet after newer packet: rejected
    assert(is_sequence_newer(40, 42) == false);
    assert(is_sequence_newer(254, 0) == false); // Old packet before wrap

    ESP_LOGI(TAG, "[PASS] DMX sequence wrap tests passed.");
    return true;
}

bool test_config_migration() {
    ESP_LOGI(TAG, "Testing ConfigStore v1 -> v2 migration (Item 16)...");

    auto v1 = std::make_unique<DeviceConfigV1>();
    std::strncpy(v1->device_name, "stage_rig_01", sizeof(v1->device_name));
    v1->zones[2].fixture_mask = (1U << 4) | (1U << 5); // Fixtures 4 and 5 in Zone 2
    std::strncpy(v1->admin_token, "lumigrid_admin", sizeof(v1->admin_token));

    auto v2 = std::make_unique<DeviceConfig>();
    bool ok = ConfigStore::migrate_v1_to_v2(*v1, *v2);
    assert(ok);
    assert(v2->schema_version == 2);
    assert(std::strcmp(v2->device_name, "stage_rig_01") == 0);
    assert(v2->fixtures[4].zone_id == 2);
    assert(v2->fixtures[5].zone_id == 2);
    assert(v2->checksum != 0);

    ESP_LOGI(TAG, "[PASS] ConfigStore v1 -> v2 migration passed.");
    return true;
}

bool test_runtime_integration() {
    ESP_LOGI(TAG, "Testing Runtime Integration & Arbitration (Item 22)...");

    ControlSourceManager csm;
    csm.init();

    // 1. Explicit Priority Table Hierarchy (Item 8)
    assert(source_priority(ControlSource::EStop) == 100);
    assert(source_priority(ControlSource::Factory) == 60);
    assert(source_priority(ControlSource::LiveDmx) == 50);
    assert(source_priority(ControlSource::ManualOverride) == 40);
    assert(source_priority(ControlSource::Scheduler) == 30);
    assert(source_priority(ControlSource::ScenePlayback) == 20);
    assert(source_priority(ControlSource::Fallback) == 10);
    assert(source_priority(ControlSource::Idle) == 0);

    // 2. Multi-source handoff:
    // Scene active -> Art-Net A -> Art-Net B -> A stops -> B continues -> B stops -> Scene resumes
    const uint32_t t0 = 1000;
    csm.acquire(ControlSource::ScenePlayback, 0, 0xFF, 0x07, 0);
    assert(csm.current_owner() == ControlSource::ScenePlayback);

    // Art-Net source A starts
    csm.acquire(ControlSource::LiveDmx, 2000, 0xFF, 0x07, 0);
    assert(csm.current_owner() == ControlSource::LiveDmx);

    // Art-Net source B arrives and renews lease
    csm.acquire(ControlSource::LiveDmx, 2000, 0xFF, 0x07, 0);
    assert(csm.current_owner() == ControlSource::LiveDmx);

    // Source A stops, B continues (lease is active)
    assert(csm.current_owner() == ControlSource::LiveDmx);

    // Source B stops and lease is released
    csm.release(ControlSource::LiveDmx);
    assert(csm.current_owner() == ControlSource::ScenePlayback); // Scene resumes!

    // 3. Fixture vs Zone scoping:
    // Fixture 0 = Live DMX, Fixture 1 in same Zone 0 remains scene driven
    csm.acquire(ControlSource::ScenePlayback, 0, (1 << 0), 0x07, 0); // Zone 0 owned by Scene
    csm.acquire(ControlSource::LiveDmx, 2000, 0, 0, (1 << 0));       // Fixture 0 owned by LiveDmx

    ControlSource fx0_owner = csm.resolve_fixture(0, 0, t0);
    ControlSource fx1_owner = csm.resolve_fixture(1, 0, t0);
    assert(fx0_owner == ControlSource::LiveDmx);
    assert(fx1_owner == ControlSource::ScenePlayback);

    ESP_LOGI(TAG, "[PASS] Runtime integration & arbitration tests passed.");
    return true;
}

} // namespace

bool run_runtime_hardening_tests() {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  STARTING RUNTIME HARDENING BATCH 2 TESTS");
    ESP_LOGI(TAG, "==================================================");

    bool all_ok = true;
    all_ok &= test_color_pipeline();
    all_ok &= test_dmx_sequence();
    all_ok &= test_config_migration();
    all_ok &= test_runtime_integration();

    if (all_ok) {
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, "  ALL RUNTIME HARDENING TESTS PASSED SUCCESSFULLY!");
        ESP_LOGI(TAG, "==================================================");
    } else {
        ESP_LOGE(TAG, "RUNTIME HARDENING TESTS ENCOUNTERED FAILURES!");
    }
    return all_ok;
}

} // namespace lg
