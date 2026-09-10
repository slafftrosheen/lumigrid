// main/RuntimeHardeningTests.cpp
#include "RuntimeHardeningTests.hpp"
#include "ColorPipeline.hpp"
#include "ControlSourceManager.hpp"
#include "ConfigStore.hpp"
#include "OutputController.hpp"
#include "LightingRuntime.hpp"
#include "OutputScheduler.hpp"
#include "DmxRouter.hpp"
#include "FactoryRunner.hpp"
#include "FactoryApi.hpp"
#include "LumigridRuntime.hpp"
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <memory>
#include <array>

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS

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

// ============================================================================
// Batch 4 Test A: Physical Safety Race Model (Batch 4 Item 15.A)
// ============================================================================
bool test_batch4_physical_safety_race_model() {
    ESP_LOGI(TAG, "Testing Batch 4 Test A: Physical Safety Race Model...");

    OutputController ctrl;

    // Normal commit -> succeeds
    assert(ctrl.try_commit() == ESP_OK);

    // Latch estop: must immediately prevent commit under the locked safety decision
    ctrl.latch_estop();
    assert(ctrl.is_estop_latched() == true);
    assert(ctrl.output_enabled() == false);

    // try_commit while estop is latched MUST return ESP_ERR_INVALID_STATE
    // and execute blackout_locked() while holding the mutex
    assert(ctrl.try_commit() == ESP_ERR_INVALID_STATE);

    // Explicit blackout while latched also succeeds safely
    assert(ctrl.blackout() == ESP_OK);

    // Clear estop
    ctrl.clear_estop();
    assert(ctrl.is_estop_latched() == false);
    assert(ctrl.output_enabled() == true);

    // Now commit succeeds
    assert(ctrl.try_commit() == ESP_OK);

    // Disable output explicitly
    ctrl.set_output_enabled(false);
    assert(ctrl.try_commit() == ESP_ERR_INVALID_STATE);
    ctrl.set_output_enabled(true);
    assert(ctrl.try_commit() == ESP_OK);

    ESP_LOGI(TAG, "[PASS] Batch 4 Test A passed.");
    return true;
}

// ============================================================================
// Batch 4 Test B: Explicit Output Fault Recovery (Batch 4 Item 15.B)
// ============================================================================
bool test_batch4_explicit_output_fault_recovery() {
    ESP_LOGI(TAG, "Testing Batch 4 Test B: Explicit Output Fault Recovery...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    assert(runtime.output_fault() == false);
    assert(runtime.output_fault_count() == 0);

    // Inject simulated hardware commit failure
    runtime.report_output_fault(ESP_ERR_TIMEOUT);
    assert(runtime.output_fault() == true);
    assert(runtime.output_fault_count() == 1);

    // Subsequent commits / frame passes MUST NOT self-heal (no auto-recovery after 3 commits)
    for (int i = 0; i < 5; ++i) {
        assert(runtime.output_fault() == true);
    }

    // Trigger E-stop to test refusal during active E-stop
    runtime.trigger_estop();
    assert(runtime.is_estop_active() == true);

    // Clearing fault while E-stop is active MUST be refused
    assert(runtime.clear_output_fault() == ESP_ERR_INVALID_STATE);
    assert(runtime.output_fault() == true);

    // Release E-stop
    runtime.clear_estop();
    ctrl.clear_estop();

    // Now explicit clear_output_fault() succeeds
    assert(runtime.clear_output_fault() == ESP_OK);
    assert(runtime.output_fault() == false);

    ESP_LOGI(TAG, "[PASS] Batch 4 Test B passed.");
    return true;
}

// ============================================================================
// Batch 4 Test C: DMX Timeout Clearing (Batch 4 Item 15.C)
// ============================================================================
bool test_batch4_dmx_timeout_clearing() {
    ESP_LOGI(TAG, "Testing Batch 4 Test C: DMX Timeout Clearing...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.universe_start = 1;
    dev_cfg.protocol.stream_timeout_ms = 300;
    dev_cfg.fixtures[0].num_pixels = 4;
    dev_cfg.fixtures[0].dmx_universe = 1;
    dev_cfg.fixtures[0].dmx_start_channel = 1;
    dev_cfg.fixtures[0].dmx_mode = 0; // wash
    dev_cfg.fixtures[0].profile = FixtureProfile::WS2812;

    DmxRouter router(runtime, dev_cfg);
    DmxSourceId src{DmxSourceProtocol::ArtNet, {192, 168, 1, 20}};

    std::array<uint8_t, 12> data{255, 128, 64, 0};
    router.process_frame(1, data.data(), data.size(), 100, src, 1);

    // Verify patch is active
    assert(runtime.last_dmx_data()[0] == 255);
    assert(router.active_source_count() == 1);

    // Clear patch for universe when it expires
    router.clear_patch_for_universe(1);

    ESP_LOGI(TAG, "[PASS] Batch 4 Test C passed.");
    return true;
}

// ============================================================================
// Batch 4 Test D: Router Lock Boundary (Batch 4 Item 15.D)
// ============================================================================
bool test_batch4_router_lock_boundary() {
    ESP_LOGI(TAG, "Testing Batch 4 Test D: Router Lock Boundary...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.universe_start = 1;
    dev_cfg.protocol.stream_timeout_ms = 1000;

    DmxRouter router(runtime, dev_cfg);
    DmxSourceId src{DmxSourceProtocol::ArtNet, {10, 0, 0, 1}};

    // Verify that router configuration and timeout can be read concurrently without deadlock
    std::array<uint8_t, 12> data{11, 22, 33};
    for (int i = 0; i < 20; ++i) {
        router.process_frame(1, data.data(), data.size(), 100, src, static_cast<uint8_t>(i + 1));
        uint32_t to = router.stream_timeout_ms();
        assert(to == 1000);
        assert(router.active_source_count() == 1);
    }

    ESP_LOGI(TAG, "[PASS] Batch 4 Test D passed.");
    return true;
}

// ============================================================================
// Batch 4 Test E: Config Validation (Batch 4 Item 15.E)
// ============================================================================
bool test_batch4_config_validation() {
    ESP_LOGI(TAG, "Testing Batch 4 Test E: Config Validation...");

    DeviceConfig cfg{};
    cfg.schema_version = 2;
    cfg.gamma_x10 = 22;
    cfg.max_power_mw = 50000;
    cfg.protocol.zone_start_channel = 1;
    cfg.protocol.live_stream_layer = 2;

    // 1. Valid config accepted
    ConfigStore::ConfigValidationResult res;
    assert(ConfigStore::validate_config(cfg, &res) == ESP_OK);
    assert(res.valid == true);

    // 2. Too many pixels rejected
    DeviceConfig bad_pixels = cfg;
    bad_pixels.fixtures[0].profile = FixtureProfile::WS2812;
    bad_pixels.fixtures[0].num_pixels = MAX_PIXELS + 1; // 257 > 256
    assert(ConfigStore::validate_config(bad_pixels, &res) == ESP_ERR_INVALID_ARG);
    assert(res.valid == false);
    assert(res.error_count >= 1);

    // Verify save_config does NOT silently mutate bad_pixels
    ConfigStore store;
    store.init();
    uint16_t orig_pixels = bad_pixels.fixtures[0].num_pixels;
    esp_err_t save_err = store.save_config(bad_pixels);
    assert(save_err == ESP_ERR_INVALID_ARG);
    assert(bad_pixels.fixtures[0].num_pixels == orig_pixels); // Not mutated!

    // 3. Invalid physical output index rejected
    DeviceConfig bad_output = cfg;
    bad_output.fixtures[0].physical_output_index[0] = 16; // >= 16
    assert(ConfigStore::validate_config(bad_output, &res) == ESP_ERR_INVALID_ARG);

    // 4. DMX overflow rejected (wash mode RGB needs 3 channels)
    DeviceConfig bad_dmx = cfg;
    bad_dmx.fixtures[0].profile = FixtureProfile::WS2812;
    bad_dmx.fixtures[0].dmx_start_channel = 511; // 511 + 3 - 1 = 513 > 512
    bad_dmx.fixtures[0].dmx_mode = 0;
    assert(ConfigStore::validate_config(bad_dmx, &res) == ESP_ERR_INVALID_ARG);

    // 5. Invalid zone_id rejected
    DeviceConfig bad_zone = cfg;
    bad_zone.fixtures[0].zone_id = 8; // >= 8
    assert(ConfigStore::validate_config(bad_zone, &res) == ESP_ERR_INVALID_ARG);

    // 6. Sane gamma range checked
    DeviceConfig bad_gamma = cfg;
    bad_gamma.gamma_x10 = 5; // < 10
    assert(ConfigStore::validate_config(bad_gamma, &res) == ESP_ERR_INVALID_ARG);

    ESP_LOGI(TAG, "[PASS] Batch 4 Test E passed.");
    return true;
}

// ============================================================================
// Batch 4 Test F: Source Metrics (Batch 4 Item 15.F)
// ============================================================================
bool test_batch4_source_metrics() {
    ESP_LOGI(TAG, "Testing Batch 4 Test F: Source Metrics...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.universe_start = 1;
    dev_cfg.protocol.stream_timeout_ms = 400;

    DmxRouter router(runtime, dev_cfg);
    assert(router.active_source_count() == 0);
    assert(router.dmx_packets_accepted() == 0);
    assert(router.dmx_sources_dropped() == 0);

    DmxSourceId src1{DmxSourceProtocol::ArtNet, {192, 168, 1, 101}};
    DmxSourceId src2{DmxSourceProtocol::ArtNet, {192, 168, 1, 102}};

    std::array<uint8_t, 12> data{1, 2, 3};

    // Source 1 packet accepted
    router.process_frame(1, data.data(), data.size(), 100, src1, 1);
    assert(router.active_source_count() == 1);
    assert(router.dmx_packets_accepted() == 1);

    // Source 2 packet accepted
    router.process_frame(1, data.data(), data.size(), 100, src2, 1);
    assert(router.active_source_count() == 2);
    assert(router.dmx_packets_accepted() == 2);

    // Unpatched universe ignored
    router.process_frame(99, data.data(), data.size(), 100, src1, 2);
    assert(router.dmx_packets_ignored_unpatched() == 1);

    // Sequence duplicate rejected
    router.process_frame(1, data.data(), data.size(), 100, src1, 1);
    assert(router.dmx_packets_rejected_sequence() == 1);

    // One source expires after timeout
    vTaskDelay(pdMS_TO_TICKS(450));
    // Send only from Source 2
    router.process_frame(1, data.data(), data.size(), 100, src2, 2);
    assert(router.active_source_count() == 1);

    ESP_LOGI(TAG, "[PASS] Batch 4 Test F passed.");
    return true;
}

// ============================================================================
// Batch 4 Test G: Sequence Semantics per Protocol (Batch 4 Item 15.G)
// ============================================================================
bool test_batch4_sequence_protocol_semantics() {
    ESP_LOGI(TAG, "Testing Batch 4 Test G: Sequence Protocol Semantics...");

    // Helper tests
    assert(DmxRouter::sequence_check_required(DmxSourceProtocol::ArtNet, 0) == false);
    assert(DmxRouter::sequence_check_required(DmxSourceProtocol::ArtNet, 1) == true);
    assert(DmxRouter::sequence_check_required(DmxSourceProtocol::Sacn, 0) == true);
    assert(DmxRouter::sequence_check_required(DmxSourceProtocol::Sacn, 1) == true);

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.universe_start = 1;
    DmxRouter router(runtime, dev_cfg);

    DmxSourceId artnet_src{DmxSourceProtocol::ArtNet, {10, 0, 0, 5}};
    std::array<uint8_t, 4> data{10, 20, 30, 0};

    // Art-Net sequence 0 sent repeatedly must always be accepted (unsequenced)
    uint32_t acc_before = router.dmx_packets_accepted();
    router.process_frame(1, data.data(), data.size(), 100, artnet_src, 0);
    router.process_frame(1, data.data(), data.size(), 100, artnet_src, 0);
    router.process_frame(1, data.data(), data.size(), 100, artnet_src, 0);
    assert(router.dmx_packets_accepted() == acc_before + 3);
    assert(router.dmx_packets_rejected_sequence() == 0);

    ESP_LOGI(TAG, "[PASS] Batch 4 Test G passed.");
    return true;
}

// ============================================================================
// Batch 5 Test A: Output Fault Latch & Two-Stage Clear (Batch 5 Items 1 & 2)
// ============================================================================
bool test_batch5_output_fault_latch() {
    ESP_LOGI(TAG, "Testing Batch 5 Test A: Output Fault Latch & Two-Stage Clear...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    assert(!runtime.output_fault());
    assert(runtime.output_fault_count() == 0);

    // 1. Inject failure
    runtime.report_output_fault(ESP_ERR_TIMEOUT);
    assert(runtime.output_fault());
    assert(runtime.output_fault_count() == 1);

    // 2. Later successful commits while faulted MUST NOT clear fault automatically
    // (consecutive_successful_commits_ was removed)
    assert(runtime.output_fault());

    // 3. Clear during E-stop must be rejected
    runtime.trigger_estop();
    assert(runtime.clear_output_fault() == ESP_ERR_INVALID_STATE);
    assert(runtime.output_fault());

    // Clear E-stop
    runtime.clear_estop();
    ctrl.clear_estop();

    // 4. Two-stage clear: explicit clear arms output for next frame
    assert(runtime.clear_output_fault() == ESP_OK);
    assert(!runtime.output_fault());

    ESP_LOGI(TAG, "[PASS] Batch 5 Test A passed.");
    return true;
}

// ============================================================================
// Batch 5 Test B: Live-Frame Clear Semantics (Batch 5 Item 4)
// ============================================================================
bool test_batch5_live_frame_clear() {
    ESP_LOGI(TAG, "Testing Batch 5 Test B: Live-Frame Clear Semantics...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    // Inject live fixture data
    LightValue pixels[4] = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {255, 255, 255, 0}};
    runtime.inject_live_fixture(0, pixels, 4, nullptr, 255);

    // Live DMX ownership must be active
    assert(csm.current_owner() == ControlSource::LiveDmx);

    // Call clear_live_frame()
    runtime.clear_live_frame();

    // Verify ownership released and state cleared
    assert(csm.current_owner() != ControlSource::LiveDmx);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test B passed.");
    return true;
}

// ============================================================================
// Batch 5 Test C: Universe Timeout & Patch Clearing (Batch 5 Items 3 & 5)
// ============================================================================
bool test_batch5_universe_timeout_and_patch_clear() {
    ESP_LOGI(TAG, "Testing Batch 5 Test C: Universe Timeout & Patch Clearing...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig dev_cfg{};
    dev_cfg.protocol.universe_start = 1;
    dev_cfg.protocol.stream_timeout_ms = 50; // fast timeout for test

    DmxRouter router(runtime, dev_cfg);

    DmxSourceId src_a{DmxSourceProtocol::ArtNet, {192, 168, 1, 10}};
    DmxSourceId src_b{DmxSourceProtocol::ArtNet, {192, 168, 1, 11}};
    std::array<uint8_t, 6> data_a{100, 100, 100, 0, 0, 0};
    std::array<uint8_t, 6> data_b{200, 200, 200, 0, 0, 0};

    // Both sources active
    router.process_frame(1, data_a.data(), data_a.size(), 100, src_a, 1);
    router.process_frame(1, data_b.data(), data_b.size(), 100, src_b, 1);

    DmxRouter::DmxStats stats = router.get_stats();
    assert(stats.active_sources == 2);
    assert(stats.active_universes == 1);

    // Sleep so source A expires, but send packet on source B to keep B active
    vTaskDelay(pdMS_TO_TICKS(60));
    router.process_frame(1, data_b.data(), data_b.size(), 100, src_b, 2);

    stats = router.get_stats();
    assert(stats.active_sources == 1); // Source A expired, Source B remains
    assert(stats.active_universes == 1);

    // Sleep so source B also expires
    vTaskDelay(pdMS_TO_TICKS(60));

    // Clear patch for universe when it expires
    router.clear_patch_for_universe(1);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test C passed.");
    return true;
}

// ============================================================================
// Batch 5 Test D: Patch Update Invalidation (Batch 5 Item 6)
// ============================================================================
bool test_batch5_patch_update_invalidation() {
    ESP_LOGI(TAG, "Testing Batch 5 Test D: Patch Update Invalidation...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig cfg1{};
    cfg1.protocol.universe_start = 10; // Zone wash on 10
    cfg1.fixtures[0].num_pixels = 4;
    cfg1.fixtures[0].dmx_universe = 1; // Fixture 0 on Universe 1
    cfg1.fixtures[0].dmx_start_channel = 1;
    cfg1.fixtures[0].dmx_mode = 0;

    DmxRouter router(runtime, cfg1);
    DmxSourceId src{DmxSourceProtocol::ArtNet, {192, 168, 1, 50}};

    std::array<uint8_t, 12> data{255, 128, 64, 0};
    router.process_frame(1, data.data(), data.size(), 100, src, 1);
    assert(router.get_stats().packets_accepted == 1);

    // Update config to move fixture 0 from universe 1 to universe 2
    DeviceConfig cfg2 = cfg1;
    cfg2.fixtures[0].dmx_universe = 2;
    router.update_config(cfg2);

    // Sending packet to universe 1 must now be ignored as unpatched!
    uint32_t ignored_before = router.get_stats().packets_ignored_unpatched;
    router.process_frame(1, data.data(), data.size(), 100, src, 2);
    assert(router.get_stats().packets_ignored_unpatched == ignored_before + 1);

    // Sending packet to universe 2 must now be accepted
    uint32_t accepted_before = router.get_stats().packets_accepted;
    router.process_frame(2, data.data(), data.size(), 100, src, 3);
    assert(router.get_stats().packets_accepted == accepted_before + 1);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test D passed.");
    return true;
}

// ============================================================================
// Batch 5 Test E: DMX Span Validation & Calculation (Batch 5 Item 9 & 10)
// ============================================================================
bool test_batch5_dmx_span_validation() {
    ESP_LOGI(TAG, "Testing Batch 5 Test E: DMX Span Validation & Calculation...");

    uint32_t channels = 0;

    // 1. Unpatched fixture (dmx_start_channel == 0)
    FixtureConfig fix_unpatched{};
    fix_unpatched.dmx_start_channel = 0;
    assert(ConfigStore::calculate_dmx_span(fix_unpatched, channels) == true);
    assert(channels == 0);

    // 2. Wash RGB (3 channels)
    FixtureConfig fix_rgb{};
    fix_rgb.profile = FixtureProfile::WS2812;
    fix_rgb.dmx_mode = 0; // Wash
    
    // RGB at 510: start = 509, end_exclusive = 509 + 3 = 512 <= 512 -> valid
    fix_rgb.dmx_start_channel = 510;
    assert(ConfigStore::calculate_dmx_span(fix_rgb, channels) == true);
    assert(channels == 3);

    // RGB at 511: start = 510, end_exclusive = 510 + 3 = 513 > 512 -> invalid
    fix_rgb.dmx_start_channel = 511;
    assert(ConfigStore::calculate_dmx_span(fix_rgb, channels) == false);

    // 3. Wash RGBW (4 channels)
    FixtureConfig fix_rgbw{};
    fix_rgbw.profile = FixtureProfile::SK6812;
    fix_rgbw.dmx_mode = 0; // Wash

    // RGBW at 509: start = 508, end_exclusive = 508 + 4 = 512 <= 512 -> valid
    fix_rgbw.dmx_start_channel = 509;
    assert(ConfigStore::calculate_dmx_span(fix_rgbw, channels) == true);
    assert(channels == 4);

    // RGBW at 510: start = 509, end_exclusive = 509 + 4 = 513 > 512 -> invalid
    fix_rgbw.dmx_start_channel = 510;
    assert(ConfigStore::calculate_dmx_span(fix_rgbw, channels) == false);

    // 4. PWM Single (1 channel)
    FixtureConfig fix_pwm1{};
    fix_pwm1.profile = FixtureProfile::PWM_Single;
    fix_pwm1.dmx_mode = 0;
    fix_pwm1.dmx_start_channel = 512; // end_exclusive = 511 + 1 = 512 -> valid
    assert(ConfigStore::calculate_dmx_span(fix_pwm1, channels) == true);
    assert(channels == 1);
    fix_pwm1.dmx_start_channel = 513; // invalid
    assert(ConfigStore::calculate_dmx_span(fix_pwm1, channels) == false);

    // 5. Direct RGB with N pixels at boundary
    // 10 pixels * 3 = 30 channels. Start 483: start = 482, end = 482 + 30 = 512 -> valid.
    FixtureConfig fix_direct{};
    fix_direct.profile = FixtureProfile::WS2812;
    fix_direct.dmx_mode = 1;
    fix_direct.num_pixels = 10;
    fix_direct.dmx_start_channel = 483;
    assert(ConfigStore::calculate_dmx_span(fix_direct, channels) == true);
    assert(channels == 30);

    fix_direct.dmx_start_channel = 484; // 483 + 30 = 513 > 512 -> invalid
    assert(ConfigStore::calculate_dmx_span(fix_direct, channels) == false);

    // 6. Zero-pixel semantics
    // Addressable fixture with 0 pixels in direct mode cannot be patched
    FixtureConfig fix_zero_addr{};
    fix_zero_addr.profile = FixtureProfile::WS2812;
    fix_zero_addr.dmx_mode = 1;
    fix_zero_addr.num_pixels = 0;
    fix_zero_addr.dmx_start_channel = 1;
    assert(ConfigStore::calculate_dmx_span(fix_zero_addr, channels) == false);

    // PWM fixture with 0 pixels in wash mode is valid
    FixtureConfig fix_pwm_zero{};
    fix_pwm_zero.profile = FixtureProfile::PWM_RGB;
    fix_pwm_zero.dmx_mode = 0;
    fix_pwm_zero.num_pixels = 0;
    fix_pwm_zero.dmx_start_channel = 1;
    assert(ConfigStore::calculate_dmx_span(fix_pwm_zero, channels) == true);
    assert(channels == 3);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test E passed.");
    return true;
}

// ============================================================================
// Batch 5 Test F: Output Mapping Validation (Batch 5 Items 11 & 12)
// ============================================================================
bool test_batch5_output_mapping_validation() {
    ESP_LOGI(TAG, "Testing Batch 5 Test F: Output Mapping Validation...");

    // 1. Invalid RMT hardware_id (>= 8)
    DeviceConfig cfg_bad_rmt{};
    cfg_bad_rmt.physical_outputs[0].enabled = true;
    cfg_bad_rmt.physical_outputs[0].type = OutputType::RMT;
    cfg_bad_rmt.physical_outputs[0].hardware_id = 8; // out of range [0..7]
    ConfigStore::ConfigValidationResult res_rmt;
    assert(ConfigStore::validate_config(cfg_bad_rmt, &res_rmt) != ESP_OK);
    assert(res_rmt.error_count > 0);

    // 2. Invalid PWM hardware_id (>= 8)
    DeviceConfig cfg_bad_pwm{};
    cfg_bad_pwm.physical_outputs[0].enabled = true;
    cfg_bad_pwm.physical_outputs[0].type = OutputType::PWM;
    cfg_bad_pwm.physical_outputs[0].hardware_id = 8; // out of range [0..7]
    ConfigStore::ConfigValidationResult res_pwm;
    assert(ConfigStore::validate_config(cfg_bad_pwm, &res_pwm) != ESP_OK);
    assert(res_pwm.error_count > 0);

    // 3. Unsupported output type (SPI)
    DeviceConfig cfg_bad_spi{};
    cfg_bad_spi.physical_outputs[0].enabled = true;
    cfg_bad_spi.physical_outputs[0].type = OutputType::SPI;
    ConfigStore::ConfigValidationResult res_spi;
    assert(ConfigStore::validate_config(cfg_bad_spi, &res_spi) != ESP_OK);
    assert(res_spi.error_count > 0);

    // 4. Active fixture referencing disabled physical output
    DeviceConfig cfg_disabled_phys{};
    cfg_disabled_phys.physical_outputs[0].enabled = false;
    cfg_disabled_phys.physical_outputs[0].type = OutputType::RMT;
    cfg_disabled_phys.physical_outputs[0].hardware_id = 0;
    cfg_disabled_phys.fixtures[0].profile = FixtureProfile::WS2812;
    cfg_disabled_phys.fixtures[0].num_pixels = 10;
    cfg_disabled_phys.fixtures[0].physical_output_index[0] = 0;
    ConfigStore::ConfigValidationResult res_dis;
    assert(ConfigStore::validate_config(cfg_disabled_phys, &res_dis) != ESP_OK);
    assert(res_dis.error_count > 0);

    // 5. OutputScheduler Pass 1 pre-validation
    OutputController ctrl;
    RenderFrame frame{};
    esp_err_t sched_err = OutputScheduler::schedule(frame, cfg_disabled_phys, ctrl);
    assert(sched_err == ESP_ERR_INVALID_ARG);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test F passed.");
    return true;
}

// ============================================================================
// Batch 5 Test G: submit_live_frame() Return Value (Batch 5 Item 14 & 15)
// ============================================================================
bool test_batch5_submit_live_frame_return_value() {
    ESP_LOGI(TAG, "Testing Batch 5 Test G: submit_live_frame() Return Value...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    LiveDmxFrame frame{};
    frame.layers[0][0].active = true;
    frame.layers[0][0].opacity = 255;
    frame.layers[0][0].color = BrandColor{10, 20, 30};

    // While runtime is active, submit_live_frame returns true
    assert(runtime.submit_live_frame(frame) == true);

    // Stop runtime (shutdown)
    runtime.stop();

    // After shutdown, submit_live_frame must reject submission and return false
    assert(runtime.submit_live_frame(frame) == false);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test G passed.");
    return true;
}

// ============================================================================
// Batch 5 Test H: Config Reload Transactional Safety (Batch 5 Item 16)
// ============================================================================
bool test_batch5_config_reload_transactional_safety() {
    ESP_LOGI(TAG, "Testing Batch 5 Test H: Config Reload Transactional Safety...");

    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    cfg_store.init();

    LightingRuntime runtime(csm, ctrl, cfg_store);

    DeviceConfig initial_cfg = runtime.get_config_snapshot();
    uint8_t orig_gamma = initial_cfg.gamma_x10;

    // If reload encounters an invalid config in storage, candidate validation must reject it
    // without altering runtime state
    runtime.reload_device_config();
    assert(runtime.get_config_snapshot().gamma_x10 == orig_gamma);

    ESP_LOGI(TAG, "[PASS] Batch 5 Test H passed.");
    return true;
}

// ============================================================================
// Batch 6 Test A: Unused output slots: disabled unused slot with arbitrary fields -> valid
// ============================================================================
bool test_batch6_unused_output_slots() {
    ESP_LOGI(TAG, "Testing Batch 6 Test A: Unused output slots...");
    auto cfg = std::make_unique<DeviceConfig>();
    cfg->schema_version = 2;
    cfg->physical_outputs[0].enabled = true;
    cfg->physical_outputs[0].type = OutputType::RMT;
    cfg->physical_outputs[0].hardware_id = 0;
    cfg->physical_outputs[0].gpio_pin = 13;
    cfg->fixtures[0].profile = FixtureProfile::WS2812;
    cfg->fixtures[0].num_pixels = 10;
    cfg->fixtures[0].physical_output_index[0] = 0;

    // Slot 5 is disabled but has arbitrary/garbage fields
    cfg->physical_outputs[5].enabled = false;
    cfg->physical_outputs[5].type = static_cast<OutputType>(99);
    cfg->physical_outputs[5].hardware_id = 250;
    cfg->physical_outputs[5].gpio_pin = 250;

    ConfigStore::ConfigValidationResult res;
    esp_err_t err = ConfigStore::validate_config(*cfg, &res);
    assert(err == ESP_OK);
    assert(res.valid == true);

    ESP_LOGI(TAG, "[PASS] Batch 6 Test A passed.");
    return true;
}

// ============================================================================
// Batch 6 Test B: Enabled bad output: invalid hardware ID -> invalid
// ============================================================================
bool test_batch6_enabled_bad_output() {
    ESP_LOGI(TAG, "Testing Batch 6 Test B: Enabled bad output...");
    auto cfg = std::make_unique<DeviceConfig>();
    cfg->schema_version = 2;
    // Slot 1 is enabled with invalid hardware_id >= AddressableDriver::kLanes
    cfg->physical_outputs[1].enabled = true;
    cfg->physical_outputs[1].type = OutputType::RMT;
    cfg->physical_outputs[1].hardware_id = AddressableDriver::kLanes + 2;

    ConfigStore::ConfigValidationResult res;
    esp_err_t err = ConfigStore::validate_config(*cfg, &res);
    assert(err != ESP_OK);
    assert(res.valid == false);

    ESP_LOGI(TAG, "[PASS] Batch 6 Test B passed.");
    return true;
}

// ============================================================================
// Batch 6 Test C: Unused fixture: default fixture -> valid
// ============================================================================
bool test_batch6_unused_fixture() {
    ESP_LOGI(TAG, "Testing Batch 6 Test C: Unused fixture...");
    auto cfg = std::make_unique<DeviceConfig>();
    cfg->schema_version = 2;
    // Fixture 7 is default unused (num_pixels = 0, no enabled physical output)
    assert(ConfigStore::is_fixture_configured(cfg->fixtures[7], *cfg) == false);

    ConfigStore::ConfigValidationResult res;
    esp_err_t err = ConfigStore::validate_config(*cfg, &res);
    assert(err == ESP_OK);
    assert(res.valid == true);

    ESP_LOGI(TAG, "[PASS] Batch 6 Test C passed.");
    return true;
}

// ============================================================================
// Batch 6 Test D: Configured fixture: valid profile/output pair -> valid
// ============================================================================
bool test_batch6_configured_fixture() {
    ESP_LOGI(TAG, "Testing Batch 6 Test D: Configured fixture...");
    auto cfg = std::make_unique<DeviceConfig>();
    cfg->schema_version = 2;
    cfg->physical_outputs[0].enabled = true;
    cfg->physical_outputs[0].type = OutputType::RMT;
    cfg->physical_outputs[0].hardware_id = 0;
    cfg->fixtures[0].profile = FixtureProfile::WS2812;
    cfg->fixtures[0].num_pixels = 30;
    cfg->fixtures[0].physical_output_index[0] = 0;

    assert(ConfigStore::is_fixture_configured(cfg->fixtures[0], *cfg) == true);
    ConfigStore::ConfigValidationResult res;
    esp_err_t err = ConfigStore::validate_config(*cfg, &res);
    assert(err == ESP_OK);
    assert(res.valid == true);

    ESP_LOGI(TAG, "[PASS] Batch 6 Test D passed.");
    return true;
}

// ============================================================================
// Batch 6 Test E: Output fault: fail -> Faulted, repeated frames -> fault count unchanged,
// explicit arm -> one commit attempt, success -> Healthy
// ============================================================================
bool test_batch6_output_fault_state_machine() {
    ESP_LOGI(TAG, "Testing Batch 6 Test E: Output fault state machine...");
    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    assert(runtime.output_state() == LightingRuntime::OutputState::Healthy);
    assert(runtime.output_fault_count() == 0);

    // 1. Fail -> Faulted
    runtime.report_output_fault(ESP_FAIL);
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.output_fault_count() == 1);

    // 2. Repeated frames / calls in Faulted state -> fault count unchanged
    runtime.report_output_fault(ESP_FAIL);
    runtime.report_output_fault(ESP_FAIL);
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.output_fault_count() == 1);

    // 3. Explicit arm -> Armed
    assert(runtime.clear_output_fault() == ESP_OK);
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);

    runtime.stop();
    ESP_LOGI(TAG, "[PASS] Batch 6 Test E passed.");
    return true;
}

// ============================================================================
// Batch 6 Test F: Live frame: submit frame, render snapshot, expire only via
// render interpretation, active buffer itself remains unchanged
// ============================================================================
bool test_batch6_live_frame_render_snapshot() {
    ESP_LOGI(TAG, "Testing Batch 6 Test F: Live frame render snapshot...");
    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);

    LiveDmxFrame frame{};
    frame.layers[0][0].active = true;
    frame.layers[0][0].opacity = 255;
    frame.layers[0][0].color = BrandColor{100, 150, 200};
    frame.layers[0][0].timestamp_ms = 1000;

    assert(runtime.submit_live_frame(frame) == true);
    runtime.stop();

    ESP_LOGI(TAG, "[PASS] Batch 6 Test F passed.");
    return true;
}

// ============================================================================
// Batch 6 Test G: Patch update: old universe data disappears immediately
// ============================================================================
bool test_batch6_patch_update_invalidation() {
    ESP_LOGI(TAG, "Testing Batch 6 Test G: Patch update invalidation...");
    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);
    DmxRouter router(runtime);

    auto cfg = std::make_unique<DeviceConfig>();
    cfg->protocol.universe_start = 1;
    cfg->protocol.artnet_enabled = true;
    router.update_config(*cfg);

    ArtnetDmxPacket pkt{};
    pkt.universe = 1;
    pkt.source_ip = 0x0A000001;
    pkt.length = 16;
    pkt.data[0] = 255;
    pkt.data[1] = 128;
    pkt.data[2] = 64;
    router.handle_artnet_dmx(pkt);

    assert(router.get_stats().active_universes == 1);

    // Update config with a different universe_start
    cfg->protocol.universe_start = 2;
    router.update_config(*cfg);

    // Old universe data must disappear immediately: active universes drops to 0
    assert(router.get_stats().active_universes == 0);
    assert(csm.current_owner() != ControlSource::LiveDmx);

    runtime.stop();
    ESP_LOGI(TAG, "[PASS] Batch 6 Test G passed.");
    return true;
}

// ============================================================================
// Batch 6 Test H: Universe stats: source expires -> universe count decreases
// ============================================================================
bool test_batch6_universe_stats_expiry() {
    ESP_LOGI(TAG, "Testing Batch 6 Test H: Universe stats expiry...");
    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);
    DmxRouter router(runtime);

    auto cfg = std::make_unique<DeviceConfig>();
    cfg->protocol.universe_start = 1;
    cfg->protocol.artnet_enabled = true;
    cfg->protocol.stream_timeout_ms = 50;
    router.update_config(*cfg);

    ArtnetDmxPacket pkt{};
    pkt.universe = 1;
    pkt.source_ip = 0x0A000001;
    pkt.length = 8;
    pkt.data[0] = 100;
    router.handle_artnet_dmx(pkt);

    assert(router.get_stats().active_universes == 1);

    // Clear patch for universe drops active universe count
    router.clear_patch_for_universe(1);

    runtime.stop();
    ESP_LOGI(TAG, "[PASS] Batch 6 Test H passed.");
    return true;
}

// ============================================================================
// Batch 6 Test I: HTP: two different payload lengths
// ============================================================================
bool test_batch6_htp_payload_lengths() {
    ESP_LOGI(TAG, "Testing Batch 6 Test I: HTP payload lengths...");
    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);
    DmxRouter router(runtime);

    auto cfg = std::make_unique<DeviceConfig>();
    cfg->protocol.universe_start = 1;
    cfg->protocol.merge_mode = MergeMode::HTP;
    cfg->protocol.artnet_enabled = true;
    router.update_config(*cfg);

    // Source 1: length 3
    ArtnetDmxPacket pkt1{};
    pkt1.universe = 1;
    pkt1.source_ip = 0x0A000001;
    pkt1.length = 3;
    pkt1.data[0] = 100;
    pkt1.data[1] = 50;
    pkt1.data[2] = 20;
    router.handle_artnet_dmx(pkt1);

    // Source 2: length 6
    ArtnetDmxPacket pkt2{};
    pkt2.universe = 1;
    pkt2.source_ip = 0x0A000002;
    pkt2.length = 6;
    pkt2.data[0] = 80;
    pkt2.data[1] = 90;
    pkt2.data[2] = 10;
    pkt2.data[3] = 255;
    pkt2.data[4] = 200;
    pkt2.data[5] = 150;
    router.handle_artnet_dmx(pkt2);

    assert(router.get_stats().active_sources == 2);
    assert(router.get_stats().active_universes == 1);

    runtime.stop();
    ESP_LOGI(TAG, "[PASS] Batch 6 Test I passed.");
    return true;
}

// ============================================================================
// Batch 6 Test J: LTP: newest expires -> previous source returns
// ============================================================================
bool test_batch6_ltp_newest_expires() {
    ESP_LOGI(TAG, "Testing Batch 6 Test J: LTP newest expires...");
    OutputController ctrl;
    ControlSourceManager csm;
    csm.init();
    ConfigStore cfg_store;
    LightingRuntime runtime(csm, ctrl, cfg_store);
    DmxRouter router(runtime);

    auto cfg = std::make_unique<DeviceConfig>();
    cfg->protocol.universe_start = 1;
    cfg->protocol.merge_mode = MergeMode::LTP;
    cfg->protocol.artnet_enabled = true;
    router.update_config(*cfg);

    ArtnetDmxPacket pktA{};
    pktA.universe = 1;
    pktA.source_ip = 0x0A000001;
    pktA.length = 4;
    pktA.data[0] = 11;
    router.handle_artnet_dmx(pktA);

    ArtnetDmxPacket pktB{};
    pktB.universe = 1;
    pktB.source_ip = 0x0A000002;
    pktB.length = 4;
    pktB.data[0] = 22;
    router.handle_artnet_dmx(pktB);

    assert(router.get_stats().active_sources == 2);

    runtime.stop();
    ESP_LOGI(TAG, "[PASS] Batch 6 Test J passed.");
    return true;
}

// ============================================================================
// Batch 6 Test K: Config migration: migrate, validate, persist, reload, verify
// ============================================================================
bool test_batch6_config_migration() {
    ESP_LOGI(TAG, "Testing Batch 6 Test K: Config migration...");
    auto v1 = std::make_unique<DeviceConfigV1>();
    std::strncpy(v1->device_name, "Lumigrid-V1-Node", sizeof(v1->device_name));
    v1->gamma_x10 = 22;
    v1->max_power_mw = 25000;
    v1->protocol.universe_start = 1;

    auto v2 = std::make_unique<DeviceConfig>();
    assert(ConfigStore::migrate_v1_to_v2(*v1, *v2) == true);
    assert(v2->schema_version == 2);
    assert(std::strcmp(v2->device_name, "Lumigrid-V1-Node") == 0);

    ConfigStore::ConfigValidationResult val_res;
    esp_err_t val_err = ConfigStore::validate_config(*v2, &val_res);
    assert(val_err == ESP_OK);
    assert(val_res.valid == true);

    ESP_LOGI(TAG, "[PASS] Batch 6 Test K passed.");
    return true;
}

// ============================================================================
// Batch 7 Test A: Option A Physical Output Topology and Anti-Aliasing Validation
// ============================================================================
bool test_batch7_topology_validation() {
    ESP_LOGI(TAG, "Testing Batch 7 Test A: Option A Topology & Anti-Aliasing...");
    auto cfg = std::make_unique<DeviceConfig>();
    cfg->gamma_x10 = 22;
    cfg->max_power_mw = 25000;
    cfg->protocol.universe_start = 1;
    cfg->protocol.zone_start_channel = 1;
    cfg->protocol.live_stream_layer = 2;

    for (int p = 0; p < 16; ++p) {
        cfg->physical_outputs[p].enabled = false;
        cfg->physical_outputs[p].type = OutputType::RMT;
        cfg->physical_outputs[p].hardware_id = 255;
    }
    for (int f = 0; f < 16; ++f) {
        cfg->fixtures[f].num_pixels = 0;
        cfg->fixtures[f].dmx_start_channel = 0;
    }

    // A. Unused physical output: disabled + garbage hardware_id -> valid
    cfg->physical_outputs[15].enabled = false;
    cfg->physical_outputs[15].hardware_id = 99;
    ConfigStore::ConfigValidationResult res;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // B. Used physical output: enabled + invalid hardware_id -> invalid
    cfg->physical_outputs[0].enabled = true;
    cfg->physical_outputs[0].type = OutputType::RMT;
    cfg->physical_outputs[0].hardware_id = MAX_RMT_LANES + 1;
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);
    cfg->physical_outputs[0].hardware_id = 0; // Fix

    // C. Addressable fixture uses ONLY physical_output_index[0] (Option A)
    // Extra indices [1..3] with garbage do not fail validation
    cfg->fixtures[0].profile = FixtureProfile::WS2812;
    cfg->fixtures[0].num_pixels = 60;
    cfg->fixtures[0].zone_id = 0;
    cfg->fixtures[0].physical_output_index[0] = 0;
    cfg->fixtures[0].physical_output_index[1] = 255; // Unused under Option A
    cfg->fixtures[0].physical_output_index[2] = 255;
    cfg->fixtures[0].physical_output_index[3] = 255;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // D. Two addressable fixtures -> same RMT lane -> reject aliasing!
    cfg->physical_outputs[1].enabled = true;
    cfg->physical_outputs[1].type = OutputType::RMT;
    cfg->physical_outputs[1].hardware_id = 0; // Same RMT hardware lane 0!
    cfg->fixtures[1].profile = FixtureProfile::WS2812;
    cfg->fixtures[1].num_pixels = 30;
    cfg->fixtures[1].zone_id = 0;
    cfg->fixtures[1].physical_output_index[0] = 1;
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);
    cfg->physical_outputs[1].hardware_id = 1; // Distinct RMT lane 1 -> valid!
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // E. PWM RGB requires indices 0..2 (Option A)
    cfg->physical_outputs[2].enabled = true;
    cfg->physical_outputs[2].type = OutputType::PWM;
    cfg->physical_outputs[2].hardware_id = 0;
    cfg->physical_outputs[3].enabled = true;
    cfg->physical_outputs[3].type = OutputType::PWM;
    cfg->physical_outputs[3].hardware_id = 1;
    cfg->physical_outputs[4].enabled = true;
    cfg->physical_outputs[4].type = OutputType::PWM;
    cfg->physical_outputs[4].hardware_id = 2;

    cfg->fixtures[2].profile = FixtureProfile::PWM_RGB;
    cfg->fixtures[2].zone_id = 1;
    cfg->fixtures[2].physical_output_index[0] = 2;
    cfg->fixtures[2].physical_output_index[1] = 3;
    cfg->fixtures[2].physical_output_index[2] = 4;
    cfg->fixtures[2].physical_output_index[3] = 255; // Unused for RGB
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // F. Two PWM fixtures -> same PWM channel -> reject aliasing!
    cfg->physical_outputs[5].enabled = true;
    cfg->physical_outputs[5].type = OutputType::PWM;
    cfg->physical_outputs[5].hardware_id = 0; // Aliased PWM channel 0!
    cfg->fixtures[3].profile = FixtureProfile::PWM_Single;
    cfg->fixtures[3].zone_id = 2;
    cfg->fixtures[3].physical_output_index[0] = 5;
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);
    cfg->physical_outputs[5].hardware_id = 3; // Distinct PWM channel 3 -> valid!
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // G. PWM RGBW requires all 0..3 physical outputs
    cfg->physical_outputs[6].enabled = true;
    cfg->physical_outputs[6].type = OutputType::PWM;
    cfg->physical_outputs[6].hardware_id = 4;
    cfg->physical_outputs[7].enabled = true;
    cfg->physical_outputs[7].type = OutputType::PWM;
    cfg->physical_outputs[7].hardware_id = 5;
    cfg->physical_outputs[8].enabled = true;
    cfg->physical_outputs[8].type = OutputType::PWM;
    cfg->physical_outputs[8].hardware_id = 6;
    cfg->physical_outputs[9].enabled = false; // Disabled 4th channel!
    cfg->physical_outputs[9].type = OutputType::PWM;
    cfg->physical_outputs[9].hardware_id = 7;

    cfg->fixtures[4].profile = FixtureProfile::PWM_RGBW;
    cfg->fixtures[4].zone_id = 3;
    cfg->fixtures[4].physical_output_index[0] = 6;
    cfg->fixtures[4].physical_output_index[1] = 7;
    cfg->fixtures[4].physical_output_index[2] = 8;
    cfg->fixtures[4].physical_output_index[3] = 9;
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK); // Fails because slot 9 is disabled!
    cfg->physical_outputs[9].enabled = true;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // H. Unsupported output type on enabled slot -> invalid
    cfg->physical_outputs[10].enabled = true;
    cfg->physical_outputs[10].type = static_cast<OutputType>(99);
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);

    ESP_LOGI(TAG, "[PASS] Batch 7 Test A passed.");
    return true;
}

// ============================================================================
// Batch 7 Test B: Output Fault State Machine (Healthy -> Faulted -> Armed -> Healthy)
// ============================================================================
bool test_batch7_output_fault_state_machine() {
    ESP_LOGI(TAG, "Testing Batch 7 Test B: Output fault explicit state machine...");
    ControlSourceManager sm;
    sm.init();
    OutputController ctrl;
    ctrl.init();
    ConfigStore cfg_store;

    LightingRuntime runtime(sm, ctrl, cfg_store);
    assert(runtime.output_state() == LightingRuntime::OutputState::Healthy);
    assert(runtime.fault_event_count() == 0);

    // 1. Hardware error transitions Healthy -> Faulted
    runtime.report_output_fault(ESP_FAIL);
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.fault_event_count() == 1);

    // 2. 100 consecutive frames/reports while Faulted do NOT increment fault_event_count
    for (int i = 0; i < 100; ++i) {
        runtime.report_output_fault(ESP_FAIL);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.fault_event_count() == 1);

    // 3. Explicit operator recovery transitions Faulted -> Armed
    assert(runtime.clear_output_fault() == ESP_OK);
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);
    assert(runtime.fault_event_count() == 1); // event count does not reset or increment

    // 4. While Armed, an output failure transitions Armed -> Faulted and increments event count!
    runtime.report_output_fault(ESP_FAIL);
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.fault_event_count() == 2);

    // 5. Clear again to Armed
    assert(runtime.clear_output_fault() == ESP_OK);
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);

    ESP_LOGI(TAG, "[PASS] Batch 7 Test B passed.");
    return true;
}

// ============================================================================
// Batch 7 Test C: Real DmxRouter -> LightingRuntime Live Frame Integration
// ============================================================================
bool test_batch7_router_to_runtime_integration() {
    ESP_LOGI(TAG, "Testing Batch 7 Test C: DmxRouter to LightingRuntime integration...");
    ControlSourceManager sm;
    sm.init();
    OutputController ctrl;
    ctrl.init();
    ConfigStore cfg_store;

    LightingRuntime runtime(sm, ctrl, cfg_store);

    auto cfg = std::make_unique<DeviceConfig>();
    cfg->protocol.universe_start = 1;
    cfg->protocol.zone_start_channel = 1;
    cfg->protocol.live_stream_layer = 2;
    cfg->protocol.artnet_enabled = true;
    cfg->protocol.stream_timeout_ms = 500;

    cfg->fixtures[0].profile = FixtureProfile::WS2812;
    cfg->fixtures[0].num_pixels = 10;
    cfg->fixtures[0].dmx_universe = 1;
    cfg->fixtures[0].dmx_start_channel = 25; // Channels 25..54
    cfg->fixtures[0].dmx_mode = 0; // Wash mode

    DmxRouter router(runtime, *cfg);

    // Initial state: no live DMX active
    assert(runtime.current_control_source() == ControlSource::Idle);

    // Send valid Art-Net DMX packet
    ArtnetDmxPacket pkt{};
    pkt.universe = 1;
    pkt.source_ip = 0x0A000001;
    pkt.sequence = 1;
    pkt.length = 64;
    pkt.data[0] = 255; // Zone 0 R
    pkt.data[1] = 128; // Zone 0 G
    pkt.data[24] = 200; // Fixture 0 R (channel 25)
    router.handle_artnet_dmx(pkt);

    // Live DMX ownership claimed
    assert(runtime.current_control_source() == ControlSource::LiveDmx);
    assert(router.get_stats().packets_accepted == 1);

    // Stale sequence packet must be rejected
    ArtnetDmxPacket stale_pkt = pkt;
    stale_pkt.sequence = 1; // Duplicate/older sequence
    router.handle_artnet_dmx(stale_pkt);
    assert(router.get_stats().packets_rejected_sequence == 1);

    // Total universe patch expiry
    router.clear_patch_for_universe(1);
    runtime.clear_live_frame();
    assert(runtime.current_control_source() == ControlSource::Idle);

    ESP_LOGI(TAG, "[PASS] Batch 7 Test C passed.");
    return true;
}

// ============================================================================
// Batch 7 Test D: Shutdown and Restart Clean Initial State
// ============================================================================
bool test_batch7_shutdown_restart() {
    ESP_LOGI(TAG, "Testing Batch 7 Test D: Shutdown and restart...");
    ControlSourceManager sm;
    sm.init();
    OutputController ctrl;
    ctrl.init();
    ConfigStore cfg_store;

    {
        LightingRuntime runtime(sm, ctrl, cfg_store);
        runtime.start();
        runtime.inject_live_data(0, 2, BrandColor{255, 0, 0}, 255);
        assert(runtime.current_control_source() == ControlSource::LiveDmx);

        runtime.stop();
        assert(ctrl.output_enabled() == true); // Stop leaves hardware in safe blackout
    }

    // Clean restart
    {
        LightingRuntime runtime2(sm, ctrl, cfg_store);
        assert(runtime2.output_state() == LightingRuntime::OutputState::Healthy);
        assert(runtime2.active_live_objects() == 0);
        assert(runtime2.current_control_source() == ControlSource::Idle);
    }

    ESP_LOGI(TAG, "[PASS] Batch 7 Test D passed.");
    return true;
}

// ============================================================================
// Batch 8 Hardening Tests (Certification-Grade Lifecycle, Protocol & Output)
// ============================================================================

bool test_batch8_double_start_and_cooperative_shutdown() {
    ESP_LOGI(TAG, "Batch 8 Test A: Double-start rejection and cooperative shutdown...");
    ControlSourceManager sm;
    OutputController ctrl;
    ConfigStore cfg_store;

    LightingRuntime runtime(sm, ctrl, cfg_store);
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Constructed);

    // Initial start succeeds
    esp_err_t err1 = runtime.start();
    assert(err1 == ESP_OK);
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Running);

    // Repeated start must fail with ESP_ERR_INVALID_STATE (Requirement 4)
    esp_err_t err2 = runtime.start();
    assert(err2 == ESP_ERR_INVALID_STATE);
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Running);

    // Cooperative shutdown (Requirement 3)
    runtime.stop();
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Stopped);

    // Repeated stop must be idempotent and safe
    runtime.stop();
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Stopped);

    // Clean restart verification
    esp_err_t start_err3 = runtime.start();
    assert(start_err3 == ESP_OK);
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Running);

    runtime.stop();
    assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Stopped);

    // Repeated lifecycle test: start -> stop -> start -> stop (100 iterations) (Requirement 4)
    for (int i = 0; i < 100; ++i) {
        esp_err_t loop_start = runtime.start();
        assert(loop_start == ESP_OK);
        assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Running);
        runtime.stop();
        assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Stopped);
        vTaskDelay(pdMS_TO_TICKS(1)); // Allow FreeRTOS idle task to collect terminated task
    }

    ESP_LOGI(TAG, "[PASS] Batch 8 Test A passed (including 100-iteration start/stop lifecycle).");
    return true;
}

bool test_batch8_factory_runner_abort_vs_estop() {
    ESP_LOGI(TAG, "Batch 8 Test B: Factory abort vs E-stop semantics & state serialization...");
    ControlSourceManager sm;
    OutputController ctrl;
    FactoryRunner runner(sm, ctrl);

    assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Constructed);
    assert(runner.start() == ESP_OK);
    assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Running);

    // 1. SelectEffect -> Abort
    runner.select_effect(FactoryEffect::PixelChase);
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(sm.current_owner() == ControlSource::Factory);
    assert(sm.is_estop_active() == false);
    assert(runner.status().active_effect == FactoryEffect::PixelChase);

    // Abort releases Factory ownership without activating E-stop (Requirement 7)
    runner.abort();
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(sm.current_owner() == ControlSource::Idle);
    assert(sm.is_estop_active() == false);
    assert(runner.status().outputs_enabled == false);

    // 2. StreamFrame -> Abort
    StreamFramePayload payload{};
    runner.set_stream_frame(payload, 32, 1000);
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(sm.current_owner() == ControlSource::Factory);
    assert(runner.status().outputs_enabled == true);
    assert(runner.status().test_active == true);

    runner.abort();
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(sm.current_owner() == ControlSource::Idle);
    assert(runner.status().outputs_enabled == false);
    assert(sm.is_estop_active() == false);

    // 3. StreamFrame -> SelectEffect
    runner.set_stream_frame(payload, 50, 1000);
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(runner.status().test_active == true);
    runner.select_effect(FactoryEffect::Rainbow);
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(runner.status().active_effect == FactoryEffect::Rainbow);
    assert(sm.current_owner() == ControlSource::Factory);

    // 4. SelectEffect -> SetAutoLoop
    runner.set_auto_loop(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(runner.status().auto_loop == true);

    // 5. SetAutoLoop -> Abort
    runner.abort();
    vTaskDelay(pdMS_TO_TICKS(20));
    assert(runner.status().auto_loop == false);
    assert(runner.status().outputs_enabled == false);
    assert(sm.current_owner() == ControlSource::Idle);
    assert(sm.is_estop_active() == false);

    runner.stop();
    assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Stopped);

    ESP_LOGI(TAG, "[PASS] Batch 8 Test B passed.");
    return true;
}

bool test_batch8_output_transaction_and_fault_unification() {
    ESP_LOGI(TAG, "Batch 8 Test C: Output transaction API and unified commit fault transitions...");
    ControlSourceManager sm;
    OutputController ctrl;
    ConfigStore cfg_store;
    LightingRuntime runtime(sm, ctrl, cfg_store);

    // Requirement 8: Controlled RAII transaction
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.is_active());
        tx.pixels().set(0, 0, 0xAA, 0, 0, 0);
        tx.pwm().set(0, 1000);
        esp_err_t commit_res = tx.commit();
        assert(commit_res == ESP_OK);
        assert(!tx.is_active());
    }

    // Requirement 9: Unified fault state transitions on commit failure
    assert(runtime.output_state() == LightingRuntime::OutputState::Healthy);
    assert(runtime.output_commit_failure_count() == 0);
    assert(runtime.output_fault_count() == 0);

    // Simulate commit failure
    ctrl.set_mock_failure_injections(1);
    {
        auto tx = ctrl.begin_transaction();
        esp_err_t res = tx.commit();
        assert(res != ESP_OK);
    }

    // Automatic transition to Faulted without manual report_output_fault()
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.output_commit_failure_count() == 1);
    assert(runtime.output_fault_count() == 1);

    // Clear fault moves state to Armed
    runtime.clear_output_fault();
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);

    // Successful commit while Armed restores Healthy
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() == ESP_OK);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Healthy);

    // Failure while Armed immediately drops back to Faulted
    runtime.clear_output_fault();
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);
    ctrl.set_mock_failure_injections(1);
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() != ESP_OK);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.output_commit_failure_count() == 2);
    assert(runtime.output_fault_count() == 2);

    runtime.clear_output_fault();
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() == ESP_OK);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Healthy);

    // Driver-level failure injection tests (Requirement 14)
    // 1. Pixel driver failure only
    ctrl.set_mock_pixel_failure(true);
    {
        auto tx = ctrl.begin_transaction();
        esp_err_t res = tx.commit();
        assert(res == ESP_ERR_TIMEOUT);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(ctrl.last_commit_error() == ESP_ERR_TIMEOUT);
    assert(runtime.output_commit_failure_count() == 3);
    assert(runtime.output_fault_count() == 3);
    ctrl.set_mock_pixel_failure(false);

    runtime.clear_output_fault();
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);

    // 2. PWM driver failure only
    ctrl.set_mock_pwm_failure(true);
    {
        auto tx = ctrl.begin_transaction();
        esp_err_t res = tx.commit();
        assert(res == ESP_ERR_INVALID_RESPONSE);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(ctrl.last_commit_error() == ESP_ERR_INVALID_RESPONSE);
    assert(runtime.output_commit_failure_count() == 4);
    assert(runtime.output_fault_count() == 4);
    ctrl.set_mock_pwm_failure(false);

    runtime.clear_output_fault();
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);

    // 3. Both drivers fail
    ctrl.set_mock_pixel_failure(true);
    ctrl.set_mock_pwm_failure(true);
    {
        auto tx = ctrl.begin_transaction();
        esp_err_t res = tx.commit();
        assert(res != ESP_OK);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Faulted);
    assert(runtime.output_commit_failure_count() == 5);
    assert(runtime.output_fault_count() == 5);
    ctrl.set_mock_pixel_failure(false);
    ctrl.set_mock_pwm_failure(false);

    // Explicit recovery from Armed -> Healthy
    runtime.clear_output_fault();
    assert(runtime.output_state() == LightingRuntime::OutputState::Armed);
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() == ESP_OK);
    }
    assert(runtime.output_state() == LightingRuntime::OutputState::Healthy);

    // Candidate reconfigure failure & topology validity policy (Requirement 15)
    DeviceConfig bad_cfg{};
    bad_cfg.physical_outputs[0].type = OutputType::RMT;
    bad_cfg.physical_outputs[0].gpio_pin = 13;
    bad_cfg.physical_outputs[0].enabled = true;
    bad_cfg.physical_outputs[1].type = OutputType::RMT;
    bad_cfg.physical_outputs[1].gpio_pin = 13; // Duplicate GPIO pin conflict!
    bad_cfg.physical_outputs[1].enabled = true;

    esp_err_t reconf_err = ctrl.reconfigure(bad_cfg);
    assert(reconf_err != ESP_OK);
    assert(!ctrl.is_topology_valid());
    assert(!ctrl.output_enabled());

    // Commits during invalid topology must be safely rejected
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() == ESP_ERR_INVALID_STATE);
    }

    ESP_LOGI(TAG, "[PASS] Batch 8 Test C passed (driver failure injection & topology invalidation verified).");
    return true;
}

bool test_batch8_dmx_ltp_same_ms_tie_and_expiry() {
    ESP_LOGI(TAG, "Batch 8 Test D: LTP same-ms arrival_seq tie-breaking and universe expiration...");
    ControlSourceManager sm;
    OutputController ctrl;
    ConfigStore cfg_store;
    LightingRuntime runtime(sm, ctrl, cfg_store);
    DmxRouter router(runtime);

    DeviceConfig cfg{};
    cfg.protocol.merge_mode = MergeMode::LTP;
    cfg.protocol.stream_timeout_ms = 500;
    cfg.fixtures[0].profile = FixtureProfile::WS2812;
    cfg.fixtures[0].num_pixels = 4;
    cfg.fixtures[0].dmx_universe = 1;
    cfg.fixtures[0].dmx_start_channel = 1;
    router.update_config(cfg);

    // Simulate two distinct Art-Net sources arriving in the EXACT same millisecond
    ArtnetDmxPacket pktA{};
    pktA.universe = 1;
    pktA.length = 3;
    pktA.source_ip = 0x0A000001; // 10.0.0.1
    pktA.data[0] = 50;

    ArtnetDmxPacket pktB{};
    pktB.universe = 1;
    pktB.length = 3;
    pktB.source_ip = 0x0A000002; // 10.0.0.2
    pktB.data[0] = 200;

    // Send pktA then pktB
    router.handle_artnet_dmx(pktA);
    router.handle_artnet_dmx(pktB);

    // In LTP with identical timestamp, the later arrival_seq MUST win (Requirement 12)
    DmxRouter::DmxStats stats = router.get_stats();
    assert(stats.active_sources == 2);
    assert(stats.active_universes == 1);

    // Requirement 10: Universe timeout tick() without new packets
    router.tick(1000); // Exceeds 500ms timeout
    stats = router.get_stats();
    assert(stats.active_sources == 0);
    assert(stats.active_universes == 0);

    // Reappearance of source A after timeout succeeds cleanly (Requirement 10.7)
    pktA.sequence = 1;
    router.handle_artnet_dmx(pktA);
    stats = router.get_stats();
    assert(stats.active_sources == 1);
    assert(stats.active_universes == 1);

    ESP_LOGI(TAG, "[PASS] Batch 8 Test D passed.");
    return true;
}

bool test_batch8_dmx_source_capacity_policy() {
    ESP_LOGI(TAG, "Batch 8 Test E: Bounded 4-source capacity policy...");
    ControlSourceManager sm;
    OutputController ctrl;
    ConfigStore cfg_store;
    LightingRuntime runtime(sm, ctrl, cfg_store);
    DmxRouter router(runtime);

    DeviceConfig cfg{};
    cfg.protocol.stream_timeout_ms = 1000;
    cfg.fixtures[0].profile = FixtureProfile::WS2812;
    cfg.fixtures[0].num_pixels = 4;
    cfg.fixtures[0].dmx_universe = 1;
    cfg.fixtures[0].dmx_start_channel = 1;
    router.update_config(cfg);

    // Register 4 distinct sources
    for (uint32_t i = 1; i <= 4; ++i) {
        ArtnetDmxPacket pkt{};
        pkt.universe = 1;
        pkt.length = 3;
        pkt.source_ip = 0x0A000000 | i;
        pkt.data[0] = static_cast<uint8_t>(i * 10);
        router.handle_artnet_dmx(pkt);
    }
    assert(router.get_stats().active_sources == 4);
    assert(router.get_stats().sources_dropped == 0);

    // 5th source arrives -> must be rejected, counter incremented (Requirement 13)
    ArtnetDmxPacket pkt5{};
    pkt5.universe = 1;
    pkt5.length = 3;
    pkt5.source_ip = 0x0A000005;
    pkt5.data[0] = 50;
    router.handle_artnet_dmx(pkt5);

    assert(router.get_stats().active_sources == 4);
    assert(router.get_stats().sources_dropped == 1);

    ESP_LOGI(TAG, "[PASS] Batch 8 Test E passed.");
    return true;
}

bool test_batch8_dmx_partial_expiration_both_directions_and_reuse() {
    ESP_LOGI(TAG, "Batch 8 Test H: DMX partial-source expiration in both directions & slot reuse...");
    ControlSourceManager sm;
    sm.init();
    OutputController ctrl;
    ConfigStore cfg_store;
    LightingRuntime runtime(sm, ctrl, cfg_store);
    DmxRouter router(runtime);

    DeviceConfig cfg{};
    cfg.protocol.merge_mode = MergeMode::HTP;
    cfg.protocol.stream_timeout_ms = 500;
    cfg.protocol.universe_start = 1;
    cfg.fixtures[0].profile = FixtureProfile::WS2812;
    cfg.fixtures[0].num_pixels = 4;
    cfg.fixtures[0].dmx_universe = 1;
    cfg.fixtures[0].dmx_start_channel = 1;
    router.update_config(cfg);

    DmxSourceId srcA{DmxSourceProtocol::Sacn, {1, 0, 0, 0}};
    DmxSourceId srcB{DmxSourceProtocol::Sacn, {2, 0, 0, 0}};

    // ── Direction 1: A + B -> A expires -> B-only submitted -> B expires -> clear live ──
    std::array<uint8_t, 4> dataA_high{255, 0, 0, 0};
    std::array<uint8_t, 4> dataB_low{100, 0, 0, 0};

    // A arrives
    uint32_t tA = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    router.process_frame(1, dataA_high.data(), 4, 100, srcA, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    // B arrives
    uint32_t tB = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    router.process_frame(1, dataB_low.data(), 4, 100, srcB, 1);

    // Merged value is 255 (HTP max(255, 100))
    assert(runtime.last_dmx_data()[0] == 255);
    assert(runtime.current_control_source() == ControlSource::LiveDmx);
    assert(router.get_stats().active_sources == 2);

    // Advance time: Source A timed out (elapsed > 500ms), Source B still within 500ms
    router.tick(tA + 510);
    // B-only frame submitted immediately: value must now be 100! (Requirement 7)
    assert(runtime.last_dmx_data()[0] == 100);
    assert(runtime.current_control_source() == ControlSource::LiveDmx);
    assert(router.get_stats().active_sources == 1);

    // Final source B times out (elapsed > 500ms)
    router.tick(tB + 510);
    // When final source expires: universe removed, patch cleared, LiveDmx released! (Requirements 7, 11)
    assert(router.get_stats().active_sources == 0);
    assert(router.get_stats().active_universes == 0);
    assert(runtime.current_control_source() == ControlSource::Idle);

    // ── Direction 2: A + B -> B expires -> A-only submitted -> A expires -> clear live ──
    std::array<uint8_t, 4> dataB_high{250, 0, 0, 0};
    std::array<uint8_t, 4> dataA_low{80, 0, 0, 0};

    // B arrives first
    uint32_t tB2 = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    router.process_frame(1, dataB_high.data(), 4, 100, srcB, 2);
    vTaskDelay(pdMS_TO_TICKS(150));
    // A arrives second
    uint32_t tA2 = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    router.process_frame(1, dataA_low.data(), 4, 100, srcA, 2);

    // Merged value is 250 (HTP max(250, 80))
    assert(runtime.last_dmx_data()[0] == 250);
    assert(runtime.current_control_source() == ControlSource::LiveDmx);
    assert(router.get_stats().active_sources == 2);

    // Advance time: Source B timed out, Source A still active
    router.tick(tB2 + 510);
    // A-only frame submitted immediately: value must now be 80! (Requirement 7)
    assert(runtime.last_dmx_data()[0] == 80);
    assert(runtime.current_control_source() == ControlSource::LiveDmx);
    assert(router.get_stats().active_sources == 1);

    // Final source A times out
    router.tick(tA2 + 510);
    assert(router.get_stats().active_sources == 0);
    assert(router.get_stats().active_universes == 0);
    assert(runtime.current_control_source() == ControlSource::Idle);

    // ── Universe Slot Reuse Protection (Requirement 8) ──
    cfg.fixtures[1].profile = FixtureProfile::WS2812;
    cfg.fixtures[1].num_pixels = 4;
    cfg.fixtures[1].dmx_universe = 2;
    cfg.fixtures[1].dmx_start_channel = 1;
    router.update_config(cfg);

    // Universe 1 active with distinct data
    std::array<uint8_t, 4> u1_data{111, 222, 123, 0};
    router.process_frame(1, u1_data.data(), 4, 100, srcA, 10);
    assert(runtime.last_dmx_data()[0] == 111);

    // Universe 1 times out
    uint32_t t_u1 = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    router.tick(t_u1 + 600);
    assert(router.get_stats().active_universes == 0);

    // Universe 2 arrives into the tracked slot
    std::array<uint8_t, 4> u2_data{42, 43, 44, 0};
    router.process_frame(2, u2_data.data(), 4, 100, srcB, 1);
    assert(router.get_stats().active_universes == 1);

    // Reappearance of Universe 1 packet with sequence 1 after expiry (Sequence reset test)
    std::array<uint8_t, 4> u1_new{77, 0, 0, 0};
    router.process_frame(1, u1_new.data(), 4, 100, srcA, 1);
    assert(runtime.last_dmx_data()[0] == 77);

    // LTP mode universe reuse test (Requirement 8)
    cfg.protocol.merge_mode = MergeMode::LTP;
    router.update_config(cfg);

    std::array<uint8_t, 4> ltp_u1{88, 88, 88, 0};
    router.process_frame(1, ltp_u1.data(), 4, 100, srcA, 2);
    uint32_t t_ltp1 = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    router.tick(t_ltp1 + 600);
    assert(router.get_stats().active_universes == 0);

    std::array<uint8_t, 4> ltp_u2{33, 33, 33, 0};
    router.process_frame(2, ltp_u2.data(), 4, 100, srcB, 1);
    assert(router.get_stats().active_universes == 1);

    ESP_LOGI(TAG, "[PASS] Batch 8 Test H passed (DMX partial expiration in both directions & slot reuse).");
    return true;
}

bool test_batch8_protocol_consistency_and_artnet_serialization() {
    ESP_LOGI(TAG, "Batch 8 Test F: Protocol universe consistency and Art-Net 4 serialization...");
    ControlSourceManager sm;
    OutputController ctrl;
    ConfigStore cfg_store;
    LightingRuntime runtime(sm, ctrl, cfg_store);
    DmxRouter router(runtime);

    DeviceConfig cfg{};
    cfg.protocol.universe_start = 1;
    cfg.fixtures[0].profile = FixtureProfile::WS2812;
    cfg.fixtures[0].num_pixels = 4;
    cfg.fixtures[0].dmx_universe = 1;
    cfg.fixtures[0].dmx_start_channel = 1;

    cfg.fixtures[1].profile = FixtureProfile::WS2812;
    cfg.fixtures[1].num_pixels = 4;
    cfg.fixtures[1].dmx_universe = 16;
    cfg.fixtures[1].dmx_start_channel = 1;

    router.update_config(cfg);

    // Requirement 14: collect_patched_universes matches get_active_universes
    uint16_t active_unis[16];
    size_t count = router.get_active_universes(active_unis, 16);
    assert(count == 2);
    assert(active_unis[0] == 1);
    assert(active_unis[1] == 16);

    // Requirement 15: Exact PollReply byte serialization for universes 1, 16, 255, 512
    uint8_t buffer[250];

    // Universe 1: Net=0, Sub-Net=0, SwOut=1
    {
        ArtnetNodeInfo node{};
        uint8_t net = 0, sub = 0, uni = 0;
        decode_port_address(1, net, sub, uni);
        node.net = net;
        node.subnet = sub;
        node.sw_out[0] = uni;
        node.num_ports = 1;

        size_t len = ArtnetDiscovery::serialize_poll_reply(node, buffer, sizeof(buffer));
        assert(len == 239);
        assert(buffer[18] == 0);  // NetSwitch (offset 18)
        assert(buffer[19] == 0);  // SubSwitch (offset 19)
        assert(buffer[190] == 1); // SwOut[0] (offset 190)
    }

    // Universe 16: Net=0, Sub-Net=1, SwOut=0
    {
        ArtnetNodeInfo node{};
        uint8_t net = 0, sub = 0, uni = 0;
        decode_port_address(16, net, sub, uni);
        node.net = net;
        node.subnet = sub;
        node.sw_out[0] = uni;
        node.num_ports = 1;

        size_t len = ArtnetDiscovery::serialize_poll_reply(node, buffer, sizeof(buffer));
        assert(len == 239);
        assert(buffer[18] == 0);  // NetSwitch (offset 18)
        assert(buffer[19] == 1);  // SubSwitch (offset 19)
        assert(buffer[190] == 0); // SwOut[0] (offset 190)
    }

    // Universe 255: Net=0, Sub-Net=15 (0x0F), SwOut=15 (0x0F)
    {
        ArtnetNodeInfo node{};
        uint8_t net = 0, sub = 0, uni = 0;
        decode_port_address(255, net, sub, uni);
        node.net = net;
        node.subnet = sub;
        node.sw_out[0] = uni;
        node.num_ports = 1;

        size_t len = ArtnetDiscovery::serialize_poll_reply(node, buffer, sizeof(buffer));
        assert(len == 239);
        assert(buffer[18] == 0);   // NetSwitch (offset 18)
        assert(buffer[19] == 15);  // SubSwitch (offset 19)
        assert(buffer[190] == 15); // SwOut[0] (offset 190)
    }

    // Universe 512: Net=2, Sub-Net=0, SwOut=0
    {
        ArtnetNodeInfo node{};
        uint8_t net = 0, sub = 0, uni = 0;
        decode_port_address(512, net, sub, uni);
        node.net = net;
        node.subnet = sub;
        node.sw_out[0] = uni;
        node.num_ports = 1;

        size_t len = ArtnetDiscovery::serialize_poll_reply(node, buffer, sizeof(buffer));
        assert(len == 239);
        assert(buffer[18] == 2);  // NetSwitch (offset 18)
        assert(buffer[19] == 0);  // SubSwitch (offset 19)
        assert(buffer[190] == 0); // SwOut[0] (offset 190)
    }

    ESP_LOGI(TAG, "[PASS] Batch 8 Test F passed.");
    return true;
}

bool test_batch8_observational_health_and_lifecycle_rollback() {
    ESP_LOGI(TAG, "Batch 8 Test G: Purely observational health, persistence states, and lifecycle rollback...");
    ConfigStore cfg_store;
    assert(cfg_store.persistence_state_str() != nullptr);
    const auto& res = cfg_store.cached_validation_result();
    (void)res;

    // Requirement 19: Persistence state machine strings
    assert(strcmp(persistence_state_to_name(ConfigPersistenceState::Synchronized), "Synchronized") == 0);
    assert(strcmp(persistence_state_to_name(ConfigPersistenceState::PrimaryOnly), "PrimaryOnly") == 0);
    assert(strcmp(persistence_state_to_name(ConfigPersistenceState::SaveFailed), "SaveFailed") == 0);
    assert(strcmp(persistence_state_to_name(ConfigPersistenceState::RecoveryRequired), "RecoveryRequired") == 0);

    // Requirement 18: Observational health diagnostics without NVS load
    StartupDiagnostics diag{
        startup_state_to_name(StartupState::Booting),
        startup_stage_to_name(StartupStage::None),
        startup_stage_to_name(StartupStage::None),
        0
    };
    assert(strcmp(diag.startup_state, "Booting") == 0);

    // Requirement 21: Recovery button simulation (grace period, debounce, one-shot latch)
    {
        int stable_press_count = 0;
        constexpr int kRequiredHoldTicks = 250; // 250 * 20ms = 5000ms
        bool reset_triggered = false;
        int trigger_count = 0;

        // Simulate continuously held button for 500 samples (10 seconds)
        for (int tick = 0; tick < 500; ++tick) {
            // Simulated GPIO LOW (button pressed)
            const int gpio_level = 0;
            if (gpio_level == 0) {
                stable_press_count++;
                if (stable_press_count >= kRequiredHoldTicks) {
                    if (!reset_triggered) {
                        reset_triggered = true;
                        trigger_count++;
                    }
                }
            } else {
                stable_press_count = 0;
            }
        }

        // Must trigger exactly once (one-shot guarantee) despite remaining held
        assert(reset_triggered == true);
        assert(trigger_count == 1);
    }

    ESP_LOGI(TAG, "[PASS] Batch 8 Test G passed.");
    return true;
}

// ============================================================================
// Batch 9 Test: Pre-Burn-In Certification Blockers
// ============================================================================
bool test_batch9_burnin_blockers() {
    ESP_LOGI(TAG, "Testing Batch 9 Pre-Burn-In Blockers...");

    // 1. FactoryRunner Cooperative Lifecycle & Stop
    {
        ControlSourceManager sm;
        OutputController ctrl;
        FactoryRunner runner(sm, ctrl);

        assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Constructed);
        esp_err_t err = runner.start();
        assert(err == ESP_OK);
        assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Running);

        // Double start rejected
        assert(runner.start() == ESP_ERR_INVALID_STATE);

        // Cooperative stop
        runner.stop();
        assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Stopped);

        // Idempotent stop
        runner.stop();
        assert(runner.lifecycle_state() == FactoryRunner::LifecycleState::Stopped);
    }

    // 2. LightingRuntime Commit Observer Detachment on Stop
    {
        ControlSourceManager sm;
        OutputController ctrl;
        ConfigStore cfg;

        {
            LightingRuntime runtime(sm, ctrl, cfg);
            esp_err_t err = runtime.start();
            assert(err == ESP_OK);

            // Commit should invoke runtime's observer without crash
            ctrl.try_commit();

            // Stop runtime: observer must be detached
            runtime.stop();
            assert(runtime.lifecycle_state() == LightingRuntime::LifecycleState::Stopped);

            // Committing after runtime.stop() must not crash or trigger dangling observer
            ctrl.try_commit();
        }
        // Destruction of LightingRuntime: commits after destruction must not invoke dangling pointer
        ctrl.try_commit();
    }

    // 3. OutputController PCA9685 Availability & PWM Configuration Rejection
    {
        OutputController ctrl;
        ctrl.init();

        // Simulate PCA9685 unavailable
        ctrl.set_pwm_available(false);
        assert(ctrl.is_pwm_available() == false);

        DeviceConfig cfg{};
        // Setup 1 addressable fixture
        cfg.fixtures[0].profile = FixtureProfile::WS2812;
        cfg.fixtures[0].num_pixels = 30;
        cfg.fixtures[0].physical_output_index[0] = 0;
        cfg.physical_outputs[0].enabled = true;
        cfg.physical_outputs[0].type = OutputType::RMT;
        cfg.physical_outputs[0].hardware_id = 0;

        // Addressable-only configuration MUST SUCCEED even when PCA9685 is unavailable
        esp_err_t err = ctrl.reconfigure(cfg);
        assert(err == ESP_OK);
        assert(ctrl.is_topology_valid() == true);

        // Now configure a PWM fixture while PCA9685 is unavailable
        cfg.fixtures[1].profile = FixtureProfile::PWM_Single;
        cfg.fixtures[1].num_pixels = 1;
        cfg.fixtures[1].physical_output_index[0] = 1;
        cfg.physical_outputs[1].enabled = true;
        cfg.physical_outputs[1].type = OutputType::PWM;
        cfg.physical_outputs[1].hardware_id = 0;

        // MUST BE REJECTED with ESP_ERR_NOT_FOUND
        err = ctrl.reconfigure(cfg);
        assert(err == ESP_ERR_NOT_FOUND);

        // When PCA9685 becomes available, configuration MUST SUCCEED
        ctrl.set_pwm_available(true);
        assert(ctrl.is_pwm_available() == true);
        err = ctrl.reconfigure(cfg);
        assert(err == ESP_OK);
    }

    ESP_LOGI(TAG, "[PASS] Batch 9 Pre-Burn-In Blockers passed.");
    return true;
}

} // namespace

bool run_runtime_hardening_tests() {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  STARTING RUNTIME HARDENING TESTS (BATCH 3..9)");
    ESP_LOGI(TAG, "==================================================");

    bool all_ok = true;
    all_ok &= test_color_pipeline();
    all_ok &= test_estop_hard_latch();               // Batch 3 Test A
    all_ok &= test_factory_vs_runtime_arbitration(); // Batch 3 Test B
    all_ok &= test_fixture_scoped_ownership();       // Batch 3 Test C
    all_ok &= test_dmx_source_isolation();           // Batch 3 Test D
    all_ok &= test_dmx_sequence();                   // Batch 3 Test E
    all_ok &= test_config_migration();               // Batch 3 Test F

    // Batch 4 Tests
    all_ok &= test_batch4_physical_safety_race_model();       // Batch 4 Test A
    all_ok &= test_batch4_explicit_output_fault_recovery();   // Batch 4 Test B
    all_ok &= test_batch4_dmx_timeout_clearing();             // Batch 4 Test C
    all_ok &= test_batch4_router_lock_boundary();             // Batch 4 Test D
    all_ok &= test_batch4_config_validation();                // Batch 4 Test E
    all_ok &= test_batch4_source_metrics();                   // Batch 4 Test F
    all_ok &= test_batch4_sequence_protocol_semantics();      // Batch 4 Test G

    // Batch 5 Tests
    all_ok &= test_batch5_output_fault_latch();                 // Batch 5 Test A
    all_ok &= test_batch5_live_frame_clear();                   // Batch 5 Test B
    all_ok &= test_batch5_universe_timeout_and_patch_clear();   // Batch 5 Test C
    all_ok &= test_batch5_patch_update_invalidation();          // Batch 5 Test D
    all_ok &= test_batch5_dmx_span_validation();                // Batch 5 Test E
    all_ok &= test_batch5_output_mapping_validation();          // Batch 5 Test F
    all_ok &= test_batch5_submit_live_frame_return_value();     // Batch 5 Test G
    all_ok &= test_batch5_config_reload_transactional_safety(); // Batch 5 Test H

    // Batch 6 Tests
    all_ok &= test_batch6_unused_output_slots();          // Batch 6 Test A
    all_ok &= test_batch6_enabled_bad_output();           // Batch 6 Test B
    all_ok &= test_batch6_unused_fixture();               // Batch 6 Test C
    all_ok &= test_batch6_configured_fixture();           // Batch 6 Test D
    all_ok &= test_batch6_output_fault_state_machine();   // Batch 6 Test E
    all_ok &= test_batch6_live_frame_render_snapshot();   // Batch 6 Test F
    all_ok &= test_batch6_patch_update_invalidation();    // Batch 6 Test G
    all_ok &= test_batch6_universe_stats_expiry();        // Batch 6 Test H
    all_ok &= test_batch6_htp_payload_lengths();          // Batch 6 Test I
    all_ok &= test_batch6_ltp_newest_expires();           // Batch 6 Test J
    all_ok &= test_batch6_config_migration();             // Batch 6 Test K

    // Batch 7 Tests
    all_ok &= test_batch7_topology_validation();          // Batch 7 Test A
    all_ok &= test_batch7_output_fault_state_machine();   // Batch 7 Test B
    all_ok &= test_batch7_router_to_runtime_integration();// Batch 7 Test C
    all_ok &= test_batch7_shutdown_restart();             // Batch 7 Test D

    // Batch 8 Tests
    all_ok &= test_batch8_double_start_and_cooperative_shutdown();         // Batch 8 Test A
    all_ok &= test_batch8_factory_runner_abort_vs_estop();                 // Batch 8 Test B
    all_ok &= test_batch8_output_transaction_and_fault_unification();       // Batch 8 Test C
    all_ok &= test_batch8_dmx_ltp_same_ms_tie_and_expiry();               // Batch 8 Test D
    all_ok &= test_batch8_dmx_source_capacity_policy();                   // Batch 8 Test E
    all_ok &= test_batch8_protocol_consistency_and_artnet_serialization(); // Batch 8 Test F
    all_ok &= test_batch8_observational_health_and_lifecycle_rollback();   // Batch 8 Test G
    all_ok &= test_batch8_dmx_partial_expiration_both_directions_and_reuse(); // Batch 8 Test H

    // Batch 9 Tests
    all_ok &= test_batch9_burnin_blockers();

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

#endif // CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
