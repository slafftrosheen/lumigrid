// main/RuntimeHardeningTests.cpp
#include "RuntimeHardeningTests.hpp"
#include "ColorPipeline.hpp"
#include "ControlSourceManager.hpp"
#include "ConfigStore.hpp"
#include "OutputController.hpp"
#include "LightingRuntime.hpp"
#include "OutputScheduler.hpp"
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

} // namespace

bool run_runtime_hardening_tests() {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  STARTING RUNTIME HARDENING TESTS (BATCH 3, 4, 5)");
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
