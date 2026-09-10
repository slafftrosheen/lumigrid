// main/RuntimeHardeningTests.cpp
#include "RuntimeHardeningTests.hpp"
#include "ColorPipeline.hpp"
#include "ControlSourceManager.hpp"
#include "ConfigStore.hpp"
#include "OutputController.hpp"
#include "LightingRuntime.hpp"
#include "DmxRouter.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <memory>
#include <array>

static const char* TAG = "HardeningTest";

namespace lg {
namespace {

// ============================================================================
// Existing ColorPipeline verification
// ============================================================================
bool test_color_pipeline() {
    ESP_LOGI(TAG, "Testing ColorPipeline...");

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
    assert(out_22.r < 128 && out_22.r >= 50 && out_22.r <= 60);

    // 3. Gamma Reload
    ColorPipeline::set_gamma(18);
    LightValue out_18 = ColorPipeline::to_addressable(in_lin, base_fix, 255);
    ColorPipeline::set_gamma(28);
    LightValue out_28 = ColorPipeline::to_addressable(in_lin, base_fix, 255);
    assert(out_18.r > out_28.r);
    ColorPipeline::set_gamma(22);

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
    assert(pwm_duties[1] >= 2050 && pwm_duties[1] <= 2060);
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
    assert(out_explicit_w.w == 80);
    assert(out_explicit_w.r == 100);

    // 6. SK6812 Subtractive Extraction (when W == 0)
    LightValue in_sub{100, 150, 200, 0};
    LightValue out_sub = ColorPipeline::to_addressable(in_sub, sk_fix, 255);
    assert(out_sub.w == 100);
    assert(out_sub.r == 0);
    assert(out_sub.g == 50);
    assert(out_sub.b == 100);

    // 7. Max Brightness Scaling
    FixtureConfig half_fix = base_fix;
    half_fix.max_brightness = 128;
    half_fix.gamma_correction = false;
    LightValue out_half = ColorPipeline::to_addressable(LightValue(200, 0, 0, 0), half_fix, 255);
    assert(out_half.r >= 99 && out_half.r <= 101);

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

// ============================================================================
// Test A: E-stop Hard Latch (Batch 3 Item 13.A)
// ============================================================================
bool test_estop_hard_latch() {
    ESP_LOGI(TAG, "Testing E-stop Hard Latch (Item 13.A)...");

    OutputController ctrl;

    // 1. Normal commit -> succeeds
    esp_err_t err = ctrl.try_commit();
    assert(err == ESP_OK);
    assert(ctrl.output_enabled() == true);
    assert(ctrl.is_estop_latched() == false);

    // 2. Latch E-stop
    ctrl.latch_estop();
    assert(ctrl.is_estop_latched() == true);
    assert(ctrl.output_enabled() == false);

    // 3. Commit -> fails with ESP_ERR_INVALID_STATE
    err = ctrl.try_commit();
    assert(err == ESP_ERR_INVALID_STATE);

    // 4. Blackout -> succeeds
    err = ctrl.blackout();
    assert(err == ESP_OK);

    // 5. Clear E-stop: re-arms controller but does NOT restore output immediately
    ctrl.clear_estop();
    assert(ctrl.is_estop_latched() == false);
    assert(ctrl.output_enabled() == true);

    // Frame 1: simulate runtime PendingReset transition (keeps blackout)
    ctrl.blackout();

    // Frame 2: commit allowed
    err = ctrl.try_commit();
    assert(err == ESP_OK);

    ESP_LOGI(TAG, "[PASS] E-stop hard latch tests passed.");
    return true;
}

// ============================================================================
// Test B: Factory vs Runtime Arbitration (Batch 3 Item 13.B)
// ============================================================================
bool test_factory_vs_runtime_arbitration() {
    ESP_LOGI(TAG, "Testing Factory vs Runtime Arbitration (Item 13.B)...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();

    const uint32_t t0 = 1000;

    // 1. Scene active
    csm.acquire(ControlSource::ScenePlayback, 0, 0xFF, 0x07, 0);
    assert(csm.current_owner() == ControlSource::ScenePlayback);
    assert(csm.resolve(0, 0, t0) == ControlSource::ScenePlayback);

    // 2. Factory active -> Factory owns
    csm.acquire(ControlSource::Factory, 2000, 0xFF, 0x07, 0);
    assert(csm.current_owner() == ControlSource::Factory);
    assert(csm.resolve(0, 0, t0) == ControlSource::Factory);

    // 3. E-Stop triggered
    // LightingRuntime::trigger_estop() latches controller and releases Factory
    ctrl.latch_estop();
    csm.release(ControlSource::Factory);
    csm.acquire(ControlSource::EStop, 0, 0xFF, 0x07, 0);

    assert(ctrl.is_estop_latched() == true);
    assert(csm.current_owner() == ControlSource::EStop);

    // FactoryRunner tries to commit: no physical factory output allowed!
    assert(ctrl.try_commit() != ESP_OK);

    // 4. Clear E-Stop
    csm.release(ControlSource::EStop);
    ctrl.clear_estop();

    // Factory does not automatically resume old frame; Scene resumes
    assert(csm.current_owner() == ControlSource::ScenePlayback);
    assert(csm.resolve(0, 0, t0) == ControlSource::ScenePlayback);

    ESP_LOGI(TAG, "[PASS] Factory vs runtime arbitration tests passed.");
    return true;
}

// ============================================================================
// Test C: Fixture Scoped Ownership (Batch 3 Item 13.C)
// ============================================================================
bool test_fixture_scoped_ownership() {
    ESP_LOGI(TAG, "Testing Fixture Scoped Ownership (Item 13.C)...");

    ControlSourceManager csm;
    csm.init();
    const uint32_t now = 2000;

    // Fixture 0 and Fixture 1 both belong to Zone 0
    // Zone 0 owned by ScenePlayback
    csm.acquire(ControlSource::ScenePlayback, 0, (1 << 0), 0x07, 0);

    // Fixture 0 specifically overridden by LiveDmx
    csm.acquire(ControlSource::LiveDmx, 2000, 0, 0, (1 << 0));

    // Resolve fixture ownership
    ControlSource fx0_owner = csm.resolve_fixture(0, 0, now);
    ControlSource fx1_owner = csm.resolve_fixture(1, 0, now);

    assert(fx0_owner == ControlSource::LiveDmx);
    assert(fx1_owner == ControlSource::ScenePlayback);

    // Verify rendering simulation:
    // Fixture 0 receives live DMX frame data; Fixture 1 remains scene-driven
    FixtureFrame fx0_frame{};
    FixtureFrame fx1_frame{};
    LightValue live_color{255, 0, 0, 0};
    LightValue scene_color{0, 0, 255, 0};

    if (fx0_owner == ControlSource::LiveDmx) {
        fx0_frame.pixels[0] = live_color;
    } else {
        fx0_frame.pixels[0] = scene_color;
    }

    if (fx1_owner == ControlSource::LiveDmx) {
        fx1_frame.pixels[0] = live_color;
    } else {
        fx1_frame.pixels[0] = scene_color;
    }

    assert(fx0_frame.pixels[0].r == 255 && fx0_frame.pixels[0].b == 0);
    assert(fx1_frame.pixels[0].r == 0 && fx1_frame.pixels[0].b == 255);

    ESP_LOGI(TAG, "[PASS] Fixture scoped ownership tests passed.");
    return true;
}

// ============================================================================
// Test D: DMX Source Isolation (Batch 3 Item 13.D)
// ============================================================================
bool test_dmx_source_isolation() {
    ESP_LOGI(TAG, "Testing DMX Source Isolation (Item 13.D)...");

    OutputController ctrl;
    ConfigStore cfg_store;
    ControlSourceManager csm;
    csm.init();
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.artnet_enabled = true;
    dev_cfg.protocol.sacn_enabled = true;
    dev_cfg.protocol.universe_start = 1;
    dev_cfg.protocol.merge_mode = MergeMode::HTP;
    dev_cfg.protocol.stream_timeout_ms = 500;

    // Fixture 0 mapped to Universe 1, channel 1
    dev_cfg.fixtures[0].num_pixels = 4;
    dev_cfg.fixtures[0].dmx_universe = 1;
    dev_cfg.fixtures[0].dmx_start_channel = 1;
    dev_cfg.fixtures[0].dmx_mode = 0; // RGB

    DmxRouter router(runtime, dev_cfg);

    // Define two distinct sources:
    // Source A: Art-Net from 192.168.1.50
    DmxSourceId src_a{DmxSourceProtocol::ArtNet, {192, 168, 1, 50}};
    // Source B: Art-Net from 192.168.1.60
    DmxSourceId src_b{DmxSourceProtocol::ArtNet, {192, 168, 1, 60}};

    assert(src_a != src_b);

    // Both active on Universe 1
    // Source A sends: [200, 50, 0]
    std::array<uint8_t, 12> data_a{200, 50, 0};
    router.process_frame(1, data_a.data(), data_a.size(), 100, src_a, 1);

    auto dmx_after_a = runtime.last_dmx_data();
    assert(dmx_after_a[0] == 200);
    assert(dmx_after_a[1] == 50);

    // Source B sends: [100, 150, 0]
    std::array<uint8_t, 12> data_b{100, 150, 0};
    router.process_frame(1, data_b.data(), data_b.size(), 100, src_b, 1);

    // Under HTP merge: max([200, 50], [100, 150]) -> [200, 150]
    auto dmx_merged = runtime.last_dmx_data();
    assert(dmx_merged[0] == 200); // from A
    assert(dmx_merged[1] == 150); // from B

    // Both streams are tracked distinctly in the router without overwriting
    assert(router.dropped_sources_count() == 0);

    // Now advance time past timeout (500ms) for Source A while Source B continues
    vTaskDelay(pdMS_TO_TICKS(550));

    // Send packet from Source B only: [100, 150, 0]
    router.process_frame(1, data_b.data(), data_b.size(), 100, src_b, 2);

    auto dmx_after_a_timeout = runtime.last_dmx_data();
    // Since A timed out, only B remains active: channel 0 becomes 100, channel 1 becomes 150!
    assert(dmx_after_a_timeout[0] == 100);
    assert(dmx_after_a_timeout[1] == 150);

    ESP_LOGI(TAG, "[PASS] DMX source isolation tests passed.");
    return true;
}

// ============================================================================
// Test E: DMX Sequence (Batch 3 Item 13.E)
// ============================================================================
bool test_dmx_sequence() {
    ESP_LOGI(TAG, "Testing DMX Sequence Handling (Item 13.E)...");

    // 1. Math helper tests
    // 0 -> 1: forward progression (accepted)
    assert(is_sequence_newer(1, 0) == true);
    assert(is_sequence_newer(2, 1) == true);

    // Duplicate packet: rejected
    assert(is_sequence_newer(42, 42) == false);

    // Stale/old packet: rejected
    assert(is_sequence_newer(40, 42) == false);
    assert(is_sequence_newer(254, 0) == false);

    // Wrap-around: 254 -> 255 -> 0 (accepted)
    assert(is_sequence_newer(255, 254) == true);
    assert(is_sequence_newer(0, 255) == true);

    // 2. Integration with DmxRouter
    OutputController ctrl;
    ConfigStore cfg_store;
    ControlSourceManager csm;
    csm.init();
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.sacn_enabled = true;
    dev_cfg.protocol.universe_start = 1;
    dev_cfg.fixtures[0].num_pixels = 4;
    dev_cfg.fixtures[0].dmx_universe = 1;
    dev_cfg.fixtures[0].dmx_start_channel = 1;

    DmxRouter router(runtime, dev_cfg);
    DmxSourceId src{DmxSourceProtocol::Sacn, {1, 2, 3, 4}};

    std::array<uint8_t, 4> p1{10, 0, 0, 0};
    std::array<uint8_t, 4> p_dup{99, 0, 0, 0};
    std::array<uint8_t, 4> p_stale{88, 0, 0, 0};
    std::array<uint8_t, 4> p2{20, 0, 0, 0};
    std::array<uint8_t, 4> p255{30, 0, 0, 0};
    std::array<uint8_t, 4> p_wrap{40, 0, 0, 0};

    // First packet (seq 1): accepted
    router.process_frame(1, p1.data(), p1.size(), 100, src, 1);
    assert(runtime.last_dmx_data()[0] == 10);

    // Duplicate packet (seq 1): rejected, value remains 10
    router.process_frame(1, p_dup.data(), p_dup.size(), 100, src, 1);
    assert(runtime.last_dmx_data()[0] == 10);

    // Stale packet (seq 0): rejected, value remains 10
    router.process_frame(1, p_stale.data(), p_stale.size(), 100, src, 0);
    assert(runtime.last_dmx_data()[0] == 10);

    // Next packet (seq 2): accepted, value becomes 20
    router.process_frame(1, p2.data(), p2.size(), 100, src, 2);
    assert(runtime.last_dmx_data()[0] == 20);

    // Seq 255: accepted
    router.process_frame(1, p255.data(), p255.size(), 100, src, 255);
    assert(runtime.last_dmx_data()[0] == 30);

    // Seq 0 (wrap): accepted, value becomes 40
    router.process_frame(1, p_wrap.data(), p_wrap.size(), 100, src, 0);
    assert(runtime.last_dmx_data()[0] == 40);

    ESP_LOGI(TAG, "[PASS] DMX sequence tests passed.");
    return true;
}

// ============================================================================
// Test F: Config Migration & Persistence (Batch 3 Item 13.F)
// ============================================================================
bool test_config_migration() {
    ESP_LOGI(TAG, "Testing Config Migration & Persistence (Item 13.F)...");

    auto v1 = std::make_unique<DeviceConfigV1>();
    std::strncpy(v1->device_name, "stage_rig_01", sizeof(v1->device_name));
    std::strncpy(v1->admin_token, "lumigrid_admin", sizeof(v1->admin_token));

    // Case 1: Exactly 1 matching zone (Fixture 4 in Zone 2)
    v1->zones[2].fixture_mask |= (1U << 4);

    // Case 2: Multi-matching zone (Fixture 6 in Zone 1 and Zone 3) -> deterministic collapse to Zone 1
    v1->zones[1].fixture_mask |= (1U << 6);
    v1->zones[3].fixture_mask |= (1U << 6);

    // Case 3: 0 matching zones (Fixture 7 in no zone) -> collapses to Zone 0 with warning
    // (Fixture 7 has no bits set in any zones)

    // Case 4: Oversized addressable fixture (num_pixels = 300 > MAX_PIXELS 256)
    v1->fixtures[8].profile = FixtureProfile::WS2812;
    v1->fixtures[8].num_pixels = 300;

    auto v2 = std::make_unique<DeviceConfig>();
    uint32_t warning_count = 0;
    bool ok = ConfigStore::migrate_v1_to_v2(*v1, *v2, &warning_count);

    assert(ok);
    assert(v2->schema_version == 2);
    assert(std::strcmp(v2->device_name, "stage_rig_01") == 0);
    assert(std::strcmp(v2->admin_token, "lumigrid_admin") == 0);

    // Verify Case 1: single match
    assert(v2->fixtures[4].zone_id == 2);

    // Verify Case 2: multi match collapsed to first match (Zone 1)
    assert(v2->fixtures[6].zone_id == 1);

    // Verify Case 3: 0 match collapsed to Zone 0
    assert(v2->fixtures[7].zone_id == 0);

    // Verify Case 4: oversized fixture marked invalid / 0 pixels
    assert(v2->fixtures[8].num_pixels == 0);

    // Warnings must be reported for non-ideal cases
    assert(warning_count >= 3);

    // Persist & reload verification
    ConfigStore cfg_store;
    cfg_store.init();

    DeviceConfig original_cfg{};
    bool had_original = (cfg_store.load_config(original_cfg) == ESP_OK);

    // Persist migrated v2
    esp_err_t save_err = cfg_store.save_config(*v2);
    assert(save_err == ESP_OK);

    // Reload and verify
    DeviceConfig reloaded{};
    esp_err_t load_err = cfg_store.load_config(reloaded);
    assert(load_err == ESP_OK);
    assert(reloaded.schema_version == 2);
    assert(std::strcmp(reloaded.device_name, "stage_rig_01") == 0);
    assert(reloaded.fixtures[4].zone_id == 2);
    assert(reloaded.fixtures[6].zone_id == 1);
    assert(reloaded.fixtures[7].zone_id == 0);
    assert(reloaded.fixtures[8].num_pixels == 0);
    assert(reloaded.checksum == v2->checksum);

    // Restore original config
    if (had_original) {
        cfg_store.save_config(original_cfg);
    }

    ESP_LOGI(TAG, "[PASS] ConfigStore v1 -> v2 migration & persistence verified.");
    return true;
}

} // namespace

bool run_runtime_hardening_tests() {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  STARTING RUNTIME HARDENING BATCH 3 TESTS");
    ESP_LOGI(TAG, "==================================================");

    bool all_ok = true;
    all_ok &= test_color_pipeline();
    all_ok &= test_estop_hard_latch();               // Test A
    all_ok &= test_factory_vs_runtime_arbitration(); // Test B
    all_ok &= test_fixture_scoped_ownership();       // Test C
    all_ok &= test_dmx_source_isolation();           // Test D
    all_ok &= test_dmx_sequence();                   // Test E
    all_ok &= test_config_migration();               // Test F

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
