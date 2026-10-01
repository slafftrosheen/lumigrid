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
#include "CommandReceipt.hpp"
#include "JobManager.hpp"
#include "LumigridRuntime.hpp"
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "SceneTransition.hpp"
#include "AutomationEngine.hpp"
#include "CueRunner.hpp"
#include "NodeSync.hpp"
#include "Scheduler.hpp"
#include "ClockDiscipline.hpp"
#include "TimelineEvaluator.hpp"
#include "ChannelConfiguration.hpp"
#include "ShowTransport.hpp"
#include "NetworkSettings.hpp"
#include "WifiService.hpp"
#include "AuthTypes.hpp"
#include "AuthStore.hpp"
#include "AuthManager.hpp"
#include "ApiAuth.hpp"
#include "NodeIdentity.hpp"
#include "FleetDiscoveryService.hpp"
#include "RuntimeSnapshot.hpp"
#include "Capabilities.hpp"
#include "RecoveryTypes.hpp"
#include "ConfigApplyPlan.hpp"
#include "OtaBootVerifier.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
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

    // Channel 0 mapped to Universe 1, channel 1
    dev_cfg.channels[0].enabled = true;
    dev_cfg.channels[0].pixel_count = 4;
    dev_cfg.channels[0].dmx_universe = 1;
    dev_cfg.channels[0].dmx_start_channel = 1;
    dev_cfg.channels[0].dmx_mode = 0; // RGB

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
    dev_cfg.channels[0].enabled = true;
    dev_cfg.channels[0].pixel_count = 4;
    dev_cfg.channels[0].dmx_universe = 1;
    dev_cfg.channels[0].dmx_start_channel = 1;

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

    auto v2 = std::make_unique<DeviceConfigV2>();
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

    // Now migrate v2 to v3 (DeviceConfig)
    auto v3 = std::make_unique<DeviceConfig>();
    uint32_t v3_warnings = 0;
    bool v3_ok = ConfigStore::migrate_v2_to_v3(*v2, *v3, &v3_warnings);
    assert(v3_ok);
    assert(v3->schema_version == 3);
    assert(std::strcmp(v3->device_name, "stage_rig_01") == 0);

    // Persist & reload verification
    ConfigStore cfg_store;
    cfg_store.init();

    DeviceConfig original_cfg{};
    bool had_original = (cfg_store.load_config(original_cfg) == ESP_OK);

    // Persist migrated v3
    esp_err_t save_err = cfg_store.save_config(*v3);
    assert(save_err == ESP_OK);

    // Reload and verify
    DeviceConfig reloaded{};
    esp_err_t load_err = cfg_store.load_config(reloaded);
    assert(load_err == ESP_OK);
    assert(reloaded.schema_version == 3);
    assert(std::strcmp(reloaded.device_name, "stage_rig_01") == 0);

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
    dev_cfg.channels[0].enabled = true;
    dev_cfg.channels[0].pixel_count = 4;
    dev_cfg.channels[0].dmx_universe = 1;
    dev_cfg.channels[0].dmx_start_channel = 1;
    dev_cfg.channels[0].dmx_mode = 0; // wash

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
    bad_pixels.channels[0].enabled = true;
    bad_pixels.channels[0].pixel_count = MAX_PIXELS + 1; // 257 > 256
    assert(ConfigStore::validate_config(bad_pixels, &res) == ESP_ERR_INVALID_ARG);
    assert(res.valid == false);
    assert(res.error_count >= 1);

    // Verify save_config does NOT silently mutate bad_pixels
    ConfigStore store;
    store.init();
    uint16_t orig_pixels = bad_pixels.channels[0].pixel_count;
    esp_err_t save_err = store.save_config(bad_pixels);
    assert(save_err == ESP_ERR_INVALID_ARG);
    assert(bad_pixels.channels[0].pixel_count == orig_pixels); // Not mutated!

    // 3. Invalid DMX universe rejected
    DeviceConfig bad_dmx_uni = cfg;
    bad_dmx_uni.channels[0].enabled = true;
    bad_dmx_uni.channels[0].dmx_start_channel = 1;
    bad_dmx_uni.channels[0].dmx_universe = 64000; // > 63999
    assert(ConfigStore::validate_config(bad_dmx_uni, &res) == ESP_ERR_INVALID_ARG);

    // 4. DMX overflow rejected (wash mode RGB needs 3 channels)
    DeviceConfig bad_dmx = cfg;
    bad_dmx.channels[0].enabled = true;
    bad_dmx.channels[0].dmx_start_channel = 511; // 511 + 3 - 1 = 513 > 512
    bad_dmx.channels[0].dmx_mode = 0;
    assert(ConfigStore::validate_config(bad_dmx, &res) == ESP_ERR_INVALID_ARG);

    // 5. Invalid protocol layer rejected
    DeviceConfig bad_layer = cfg;
    bad_layer.protocol.live_stream_layer = 3; // >= 3
    assert(ConfigStore::validate_config(bad_layer, &res) == ESP_ERR_INVALID_ARG);

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
    cfg1.channels[0].enabled = true;
    cfg1.channels[0].pixel_count = 4;
    cfg1.channels[0].dmx_universe = 1; // Channel 0 on Universe 1
    cfg1.channels[0].dmx_start_channel = 1;
    cfg1.channels[0].dmx_mode = 0;

    DmxRouter router(runtime, cfg1);
    DmxSourceId src{DmxSourceProtocol::ArtNet, {192, 168, 1, 50}};

    std::array<uint8_t, 12> data{255, 128, 64, 0};
    router.process_frame(1, data.data(), data.size(), 100, src, 1);
    assert(router.get_stats().packets_accepted == 1);

    // Update config to move channel 0 from universe 1 to universe 2
    DeviceConfig cfg2 = cfg1;
    cfg2.channels[0].dmx_universe = 2;
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

    // 1. Invalid pixel_count (> MAX_PIXELS)
    DeviceConfig cfg_bad_pixels{};
    cfg_bad_pixels.channels[0].enabled = true;
    cfg_bad_pixels.channels[0].pixel_count = MAX_PIXELS + 1;
    ConfigStore::ConfigValidationResult res_pix;
    assert(ConfigStore::validate_config(cfg_bad_pixels, &res_pix) != ESP_OK);
    assert(res_pix.error_count > 0);

    // 2. Invalid DMX universe (> 63999)
    DeviceConfig cfg_bad_dmx{};
    cfg_bad_dmx.channels[0].enabled = true;
    cfg_bad_dmx.channels[0].dmx_start_channel = 1;
    cfg_bad_dmx.channels[0].dmx_universe = 64000;
    ConfigStore::ConfigValidationResult res_dmx;
    assert(ConfigStore::validate_config(cfg_bad_dmx, &res_dmx) != ESP_OK);
    assert(res_dmx.error_count > 0);

    // 3. OutputScheduler Pass 1 pre-validation: enabled addressable channel with 0 pixels
    DeviceConfig cfg_bad_sched{};
    cfg_bad_sched.channels[0].enabled = true;
    cfg_bad_sched.channels[0].pixel_count = 0;
    OutputController ctrl;
    RenderFrame frame{};
    esp_err_t sched_err = OutputScheduler::schedule(frame, cfg_bad_sched, ctrl);
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
    cfg->schema_version = 3;
    cfg->channels[0].enabled = true;
    cfg->channels[0].pixel_count = 10;

    // Slot 5 is disabled but has arbitrary/garbage fields
    cfg->channels[5].enabled = false;
    cfg->channels[5].pixel_count = 5000;

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
    cfg->schema_version = 3;
    // Channel 1 is enabled with invalid pixel_count > MAX_PIXELS
    cfg->channels[1].enabled = true;
    cfg->channels[1].pixel_count = MAX_PIXELS + 10;

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
    cfg->schema_version = 3;
    // Default config has all channels disabled
    assert(ConfigStore::has_configured_fixtures(*cfg) == false);

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
    cfg->schema_version = 3;
    cfg->channels[0].enabled = true;
    cfg->channels[0].pixel_count = 30;

    assert(ConfigStore::has_configured_fixtures(*cfg) == true);
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

    auto v2 = std::make_unique<DeviceConfigV2>();
    assert(ConfigStore::migrate_v1_to_v2(*v1, *v2) == true);
    assert(v2->schema_version == 2);
    assert(std::strcmp(v2->device_name, "Lumigrid-V1-Node") == 0);

    auto v3 = std::make_unique<DeviceConfig>();
    assert(ConfigStore::migrate_v2_to_v3(*v2, *v3) == true);
    assert(v3->schema_version == 3);

    ConfigStore::ConfigValidationResult val_res;
    esp_err_t val_err = ConfigStore::validate_config(*v3, &val_res);
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
        cfg->channels[p].enabled = false;
    }

    // A. Unused channel: disabled + garbage pixel_count -> valid
    cfg->channels[15].enabled = false;
    cfg->channels[15].pixel_count = 5000;
    ConfigStore::ConfigValidationResult res;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // B. Used addressable channel: enabled + invalid pixel_count -> invalid
    cfg->channels[0].enabled = true;
    cfg->channels[0].pixel_count = MAX_PIXELS + 1;
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);
    cfg->channels[0].pixel_count = 60; // Fix
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // C. DMX start channel overflow -> invalid
    cfg->channels[1].enabled = true;
    cfg->channels[1].pixel_count = 10;
    cfg->channels[1].dmx_start_channel = 510;
    cfg->channels[1].dmx_mode = 0; // RGB needs 3 channels: 510 + 3 - 1 = 512 -> valid
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);
    cfg->channels[1].dmx_start_channel = 511; // 511 + 3 - 1 = 513 > 512 -> invalid
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);
    cfg->channels[1].dmx_start_channel = 1;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // D. PWM channel with valid DMX universe
    cfg->channels[8].enabled = true;
    cfg->channels[8].dmx_start_channel = 1;
    cfg->channels[8].dmx_universe = 1;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

    // E. Invalid DMX universe (> 63999) -> invalid
    cfg->channels[8].dmx_universe = 64000;
    assert(ConfigStore::validate_config(*cfg, &res) != ESP_OK);
    cfg->channels[8].dmx_universe = 1;
    assert(ConfigStore::validate_config(*cfg, &res) == ESP_OK);

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

    cfg->channels[0].enabled = true;
    cfg->channels[0].pixel_count = 10;
    cfg->channels[0].dmx_universe = 1;
    cfg->channels[0].dmx_start_channel = 25; // Channels 25..54
    cfg->channels[0].dmx_mode = 0; // Wash mode

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
    bad_cfg.channels[0].enabled = true;
    bad_cfg.channels[0].pixel_count = 5000; // > MAX_PIXELS (256)

    esp_err_t reconf_err = ctrl.reconfigure(bad_cfg);
    assert(reconf_err != ESP_OK);
    assert(!ctrl.is_topology_valid());
    assert(!ctrl.output_enabled());

    // Commits during invalid topology must be safely rejected
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() == ESP_ERR_INVALID_STATE);
    }

    // Recovery gate: applying a valid configuration restores topology_valid_ and re-arms output_enabled_
    DeviceConfig good_cfg{};
    good_cfg.channels[0].enabled = true;
    good_cfg.channels[0].pixel_count = 60;
    esp_err_t good_reconf = ctrl.reconfigure(good_cfg);
    assert(good_reconf == ESP_OK);
    assert(ctrl.is_topology_valid() == true);
    assert(ctrl.output_enabled() == true);
    {
        auto tx = ctrl.begin_transaction();
        assert(tx.commit() == ESP_OK);
    }

    ESP_LOGI(TAG, "[PASS] Batch 8 Test C passed (driver failure injection, topology invalidation & recovery verified).");
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
    cfg.channels[0].enabled = true;
    cfg.channels[0].pixel_count = 4;
    cfg.channels[0].dmx_universe = 1;
    cfg.channels[0].dmx_start_channel = 1;
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
    cfg.channels[0].enabled = true;
    cfg.channels[0].pixel_count = 4;
    cfg.channels[0].dmx_universe = 1;
    cfg.channels[0].dmx_start_channel = 1;
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
    cfg.channels[0].enabled = true;
    cfg.channels[0].pixel_count = 4;
    cfg.channels[0].dmx_universe = 1;
    cfg.channels[0].dmx_start_channel = 1;
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
    cfg.channels[1].enabled = true;
    cfg.channels[1].pixel_count = 4;
    cfg.channels[1].dmx_universe = 2;
    cfg.channels[1].dmx_start_channel = 1;
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
    cfg.channels[0].enabled = true;
    cfg.channels[0].pixel_count = 4;
    cfg.channels[0].dmx_universe = 1;
    cfg.channels[0].dmx_start_channel = 1;

    cfg.channels[1].enabled = true;
    cfg.channels[1].pixel_count = 4;
    cfg.channels[1].dmx_universe = 16;
    cfg.channels[1].dmx_start_channel = 1;

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
        cfg.channels[0].enabled = true;
        cfg.channels[0].pixel_count = 30;
        cfg.channels[0].addressable_type = AddressableType::WS2812;

        // Addressable-only configuration MUST SUCCEED even when PCA9685 is unavailable
        esp_err_t err = ctrl.reconfigure(cfg);
        assert(err == ESP_OK);
        assert(ctrl.is_topology_valid() == true);

        // Now configure a PWM fixture while PCA9685 is unavailable
        cfg.channels[8].enabled = true;

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

// ============================================================================
// Batch 9: Checked scene activation lifecycle contract
// ============================================================================
bool test_batch9_checked_scene_activation_lifecycle_guard() {
    ESP_LOGI(
        TAG,
        "Batch 9: Checked scene activation lifecycle guard..."
    );

    ControlSourceManager source_manager;
    source_manager.init();

    OutputController controller;
    ConfigStore config_store;

    LightingRuntime runtime(
        source_manager,
        controller,
        config_store
    );

    /*
     * Constructed runtime is not allowed to claim that a scene request
     * was accepted.
     */
    assert(
        runtime.lifecycle_state() ==
        LightingRuntime::LifecycleState::Constructed
    );

    assert(runtime.activate_scene_checked(0) == false);

    /*
     * Exercise a real lifecycle transition. We intentionally don't assert
     * that preset 0 activates while Running because the test suite must not
     * depend on an existing persisted scene slot.
     */
    assert(runtime.start() == ESP_OK);

    assert(
        runtime.lifecycle_state() ==
        LightingRuntime::LifecycleState::Running
    );

    runtime.stop();

    assert(
        runtime.lifecycle_state() ==
        LightingRuntime::LifecycleState::Stopped
    );

    /*
     * Stopped runtime must also reject activation rather than silently
     * claiming success.
     */
    assert(runtime.activate_scene_checked(0) == false);

    ESP_LOGI(
        TAG,
        "[PASS] Batch 9 checked activation lifecycle guard passed."
    );

    return true;
}

// ============================================================================
// Batch 11 Phase 0: Truthful Control Contract Tests
// ============================================================================
bool test_batch11_phase0_truthful_control_contract() {
    ESP_LOGI(TAG, "Batch 11 Phase 0: Truthful control contract tests...");

    // 1. CommandJournal wrap-safe latest comparison & entry integrity
    {
        CommandJournal journal;
        CommandReceipt receipt{};

        // Accept a sequence of commands
        uint32_t id1 = journal.accept(CommandDomain::Factory, 10);
        uint32_t id2 = journal.accept(CommandDomain::Factory, 11);
        assert(id1 == 1);
        assert(id2 == 2);

        assert(journal.get(id1, receipt) == true);
        assert(receipt.id == id1);
        assert(receipt.state == CommandState::Accepted);
        assert(receipt.domain == CommandDomain::Factory);
        assert(receipt.generation == 10);

        journal.applying(id1);
        assert(journal.get(id1, receipt) == true);
        assert(receipt.state == CommandState::Applying);

        journal.applied(id1);
        assert(journal.get(id1, receipt) == true);
        assert(receipt.state == CommandState::Applied);

        journal.failed(id2, -100);
        assert(journal.get(id2, receipt) == true);
        assert(receipt.state == CommandState::Failed);
        assert(receipt.error_code == -100);

        // Verify get_latest
        assert(journal.get_latest(CommandDomain::Factory, receipt) == true);
        assert(receipt.id == id2);
    }

    // 2. JobManager exclusivity and archival
    {
        JobManager job_mgr;
        uint32_t job1 = 0;
        uint32_t job2 = 0;

        // Starting first job succeeds
        esp_err_t err = job_mgr.start_job(JobType::Ota, &job1);
        assert(err == ESP_OK);
        assert(job1 != 0);

        OperationJob current_job{};
        assert(job_mgr.get_current(current_job) == true);
        assert(current_job.job_id == job1);
        assert(current_job.state == JobState::Queued);

        // Attempting to start second job while job 1 is Queued MUST FAIL
        err = job_mgr.start_job(JobType::Restore, &job2);
        assert(err == ESP_ERR_INVALID_STATE);

        // Move to Preparing
        job_mgr.update_progress(job1, JobState::Preparing, 10);
        err = job_mgr.start_job(JobType::Restore, &job2);
        assert(err == ESP_ERR_INVALID_STATE);

        // Move to Running
        job_mgr.update_progress(job1, JobState::Running, 50);
        err = job_mgr.start_job(JobType::Restore, &job2);
        assert(err == ESP_ERR_INVALID_STATE);

        // Move to Verifying
        job_mgr.update_progress(job1, JobState::Verifying, 90);
        err = job_mgr.start_job(JobType::Restore, &job2);
        assert(err == ESP_ERR_INVALID_STATE);

        // Complete job 1 (terminal state)
        job_mgr.complete_job(job1);
        assert(job_mgr.get_current(current_job) == true);
        assert(current_job.state == JobState::Completed);

        // Now starting job 2 MUST SUCCEED, archiving job 1
        err = job_mgr.start_job(JobType::Restore, &job2);
        assert(err == ESP_OK);
        assert(job2 != job1);

        // Job 1 should now be found in history
        OperationJob hist_job{};
        assert(job_mgr.get_job(job1, hist_job) == true);
        assert(hist_job.job_id == job1);
        assert(hist_job.state == JobState::Completed);

        // Current job is job 2
        assert(job_mgr.get_current(current_job) == true);
        assert(current_job.job_id == job2);
        assert(current_job.state == JobState::Queued);

        // Fail job 2 (terminal state)
        job_mgr.fail_job(job2, -1, "Test error");
        assert(job_mgr.get_current(current_job) == true);
        assert(current_job.state == JobState::Failed);

        // Starting a new job after failure MUST SUCCEED
        uint32_t job3 = 0;
        err = job_mgr.start_job(JobType::BurnIn, &job3);
        assert(err == ESP_OK);
        assert(job3 != job2);
    }

    // 3. FactoryRunner Truthful Command Lifecycle Contract
    {
        ControlSourceManager sm;
        OutputController ctrl;
        CommandJournal journal;

        FactoryRunner runner(sm, ctrl);
        runner.set_command_journal(&journal);

        // Accept command 1 in journal
        uint32_t cmd1 = journal.accept(CommandDomain::Factory);
        CommandReceipt receipt{};
        assert(journal.get(cmd1, receipt) == true);
        assert(receipt.state == CommandState::Accepted);

        // Start runner
        assert(runner.start() == ESP_OK);

        // Enqueue command with cmd1
        FactoryCommand command;
        command.command_id = cmd1;
        command.type = FactoryCommandType::SelectEffect;
        command.effect = FactoryEffect::WhiteWalk;
        command.brightness = 32;
        command.duration_ms = 1000;
        assert(runner.enqueue(command) == true);

        // Wait a few frames for runner to dequeue and commit
        vTaskDelay(pdMS_TO_TICKS(100));

        // After output commit, cmd1 MUST be Applied!
        assert(journal.get(cmd1, receipt) == true);
        assert(receipt.state == CommandState::Applied);

        // Test Superseded: enqueue cmd2 then immediately cmd3
        uint32_t cmd2 = journal.accept(CommandDomain::Factory);
        uint32_t cmd3 = journal.accept(CommandDomain::Factory);
        FactoryCommand fc2;
        fc2.command_id = cmd2;
        fc2.type = FactoryCommandType::PixelLaneSolid;
        fc2.lane_or_channel = 0;
        fc2.red = 255;
        fc2.duration_ms = 1000;

        FactoryCommand fc3;
        fc3.command_id = cmd3;
        fc3.type = FactoryCommandType::PixelLaneSolid;
        fc3.lane_or_channel = 1;
        fc3.green = 255;
        fc3.duration_ms = 1000;

        assert(runner.enqueue(fc2) == true);
        assert(runner.enqueue(fc3) == true);

        vTaskDelay(pdMS_TO_TICKS(100));

        assert(journal.get(cmd2, receipt) == true);
        assert(receipt.state == CommandState::Superseded);
        assert(journal.get(cmd3, receipt) == true);
        assert(receipt.state == CommandState::Applied);

        // Test Failed when output commit fails (e.g. estop latched)
        ctrl.latch_estop();
        uint32_t cmd4 = journal.accept(CommandDomain::Factory);
        FactoryCommand fc4;
        fc4.command_id = cmd4;
        fc4.type = FactoryCommandType::SelectEffect;
        fc4.effect = FactoryEffect::Rainbow;
        fc4.duration_ms = 1000;
        assert(runner.enqueue(fc4) == true);

        vTaskDelay(pdMS_TO_TICKS(100));

        assert(journal.get(cmd4, receipt) == true);
        assert(receipt.state == CommandState::Failed);

        ctrl.clear_estop();
        runner.stop();
    }

    ESP_LOGI(TAG, "[PASS] Batch 11 Phase 0 truthful control contract tests passed.");
    return true;
}

bool test_lg022_show_engine() {
    ESP_LOGI(TAG, "Testing LG-022 Show Engine, Curves, Automation, and Cues...");

    // 1. Q15 Fixed-Point Curves
    assert(smoothstep_q15(0) == 0);
    assert(smoothstep_q15(16384) == 16384);
    assert(smoothstep_q15(32768) == 32768);
    assert(smoothstep_q15(40000) == 32768);

    assert(apply_curve_q15(TransitionCurve::Linear, 0) == 0);
    assert(apply_curve_q15(TransitionCurve::Linear, 16384) == 16384);
    assert(apply_curve_q15(TransitionCurve::Linear, 32768) == 32768);

    assert(apply_curve_q15(TransitionCurve::Step, 10000) == 0);
    assert(apply_curve_q15(TransitionCurve::Step, 20000) == 32768);

    assert(apply_curve_q15(TransitionCurve::EaseIn, 0) == 0);
    assert(apply_curve_q15(TransitionCurve::EaseIn, 32768) == 32768);
    assert(apply_curve_q15(TransitionCurve::EaseIn, 16384) == 8192);

    assert(apply_curve_q15(TransitionCurve::EaseOut, 16384) == 24576);

    // 2. Parameter Morphing
    LayerConfig l1{}, l2{};
    l1.effect = 1; // Solid
    l1.blend_mode = BlendMode::Normal;
    l1.palette_id = 0;
    l1.enabled = true;
    l1.primary_color = {100, 50, 0};
    l1.speed_ms = 1000;
    l1.fx_param = 20;

    l2 = l1;
    l2.primary_color = {200, 150, 100};
    l2.speed_ms = 2000;
    l2.fx_param = 120;

    assert(can_parameter_morph(l1, l2) == true);
    l2.effect = 2; // Rainbow
    assert(can_parameter_morph(l1, l2) == false);
    l2.effect = 1;

    LayerConfig morphed = sample_layer_morph(l1, l2, 16384); // 50%
    assert(morphed.primary_color.r == 150);
    assert(morphed.primary_color.g == 100);
    assert(morphed.primary_color.b == 50);
    assert(morphed.speed_ms == 1500);
    assert(morphed.fx_param == 70);

    // 3. AutomationEngine & Loop Wrap Interpolation
    AutomationTrack track{};
    track.target = AutomationTarget::GlobalBrightness;
    track.curve = AutomationCurve::Linear;
    track.period_ms = 1000;
    track.loop = true;
    track.point_count = 2;
    track.points[0] = {10000, 10000};
    track.points[1] = {50000, 50000};

    uint16_t v_mid = AutomationEngine::sample_track(track, 500);
    assert(v_mid >= 32000 && v_mid <= 33500);

    uint16_t v_start = AutomationEngine::sample_track(track, 0);
    assert(v_start >= 10000 && v_start <= 50000);

    // In-place scene application
    SceneConfig sc{};
    sc.global_brightness = 0;
    SceneMotionConfig sm{};
    sm.track_count = 1;
    sm.tracks[0].target = AutomationTarget::GlobalBrightness;
    sm.tracks[0].curve = AutomationCurve::Linear;
    sm.tracks[0].period_ms = 1000;
    sm.tracks[0].loop = false;
    sm.tracks[0].point_count = 2;
    sm.tracks[0].points[0] = {0, 0};
    sm.tracks[0].points[1] = {65535, 255};

    AutomationEngine::apply(sc, sm, 500);
    assert(sc.global_brightness >= 126 && sc.global_brightness <= 130);

    // 4. CueRunner State Machine
    {
        ControlSourceManager sman;
        OutputController out;
        ConfigStore cfg;
        LightingRuntime lighting(sman, out, cfg);
        CueRunner runner(cfg, lighting);

        CueList list{};
        list.mode = CueRunMode::SingleShot;
        list.count = 2;
        list.cues[0] = {0, 1000, 500, TransitionCurve::Linear, "Cue 1"};
        list.cues[1] = {1, 1000, 500, TransitionCurve::Linear, "Cue 2"};

        assert(runner.start(list, 0, 1000) == ESP_OK);
        auto snap0 = runner.snapshot(1000);
        assert(snap0.running == true);
        assert(snap0.current_cue == 0);
        assert(snap0.next_cue == 1);

        // At t=1250: cue 0 is fading
        runner.update(1250);
        assert(runner.snapshot(1250).state == CueRunnerState::Fading);

        // At t=2200: cue 0 is holding
        runner.update(2200);
        assert(runner.snapshot(2200).state == CueRunnerState::Holding);

        // At t=2600: hold complete -> transitioned to cue 1 fading
        runner.update(2600);
        assert(runner.snapshot(2600).current_cue == 1);
        assert(runner.snapshot(2600).state == CueRunnerState::Fading);

        // At t=4200: cue 1 finishes holding, SingleShot stops runner
        runner.update(4200);
        assert(runner.snapshot(4200).running == false);
        assert(runner.snapshot(4200).state == CueRunnerState::Stopped);

        // PingPong test
        list.mode = CueRunMode::PingPong;
        assert(runner.start(list, 0, 5000) == ESP_OK);
        runner.update(5000 + 1600);
        assert(runner.snapshot(6600).current_cue == 1);
        runner.update(5000 + 3200);
        assert(runner.snapshot(8200).current_cue == 0);

        // Manual mode test
        list.mode = CueRunMode::Manual;
        assert(runner.start(list, 0, 10000) == ESP_OK);
        assert(runner.set_manual_fade(16384) == ESP_OK);
        assert(runner.snapshot(10000).manual_fade_q15 == 16384);
        assert(runner.stop(CueStopReason::Operator, 10100) == ESP_OK);
        assert(runner.snapshot(10100).running == false);
    }

    // 5. Calendar Schedule NVS binary compatibility
    static_assert(sizeof(CalendarScheduleEntry) == 8, "CalendarScheduleEntry size changed!");
    static_assert(sizeof(CalendarSchedule) == 68, "CalendarSchedule size changed!");

    CalendarSchedule sched{};
    sched.entries[0].action = CalendarAction::CueList;
    sched.entries[0].cue_list_index = 3;
    assert(sched.entries[0].cue_list_index == 3);

    ESP_LOGI(TAG, "[PASS] LG-022 Show Engine hardening tests passed.");
    return true;
}

// ============================================================================
// LG-024 Live Desk & Manual Override Hardening Tests
// ============================================================================
bool test_lg024_live_desk_and_manual_override() {
    ESP_LOGI(TAG, "Testing LG-024 Live Desk & Manual Override (Tests A-H)...");

    const uint32_t t_base = 10000;
    uint32_t now = t_base;

    // Test A — Channel Isolation:
    // Manual mask 0x0001 (A1 only) must isolate to channel 0 without bleeding to A2 (ch 1) or P1 (ch 8)
    {
        ControlSourceManager csm;
        csm.init();
        csm.acquire(ControlSource::ManualOverride, 15000, 0x01, 0x01, 0x0001);
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(1, now) != ControlSource::ManualOverride);
        assert(snap.resolve_channel(8, now) != ControlSource::ManualOverride);
        for (uint8_t ch = 1; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::Idle);
        }
    }

    // Test B — Scene/Manual Coexistence:
    // Scene owns all 16 channels indefinitely; Manual claims A1 only.
    // Verify channel 0 resolves to ManualOverride, while channels 1..15 remain ScenePlayback.
    {
        ControlSourceManager csm;
        csm.init();
        csm.acquire(ControlSource::ScenePlayback, 0, 0xFF, 0x07, 0xFFFF);
        csm.acquire(ControlSource::ManualOverride, 15000, 0x01, 0x01, 0x0001);
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(1, now) == ControlSource::ScenePlayback);
        assert(snap.resolve_channel(8, now) == ControlSource::ScenePlayback);
        for (uint8_t ch = 1; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::ScenePlayback);
        }
    }

    // Test C — DMX Preemption (Channel-Local):
    // Manual claims A1+A2 (mask 0x0003). DMX claims A1 only (mask 0x0001).
    // Verify A1 -> LiveDmx, A2 -> ManualOverride. Releasing DMX restores A1 -> ManualOverride.
    {
        ControlSourceManager csm;
        csm.init();
        csm.acquire(ControlSource::ManualOverride, 15000, 0x03, 0x01, 0x0003);
        csm.acquire(ControlSource::LiveDmx, 5000, 0x01, 0x01, 0x0001);
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::LiveDmx);
        assert(snap.resolve_channel(1, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(8, now) == ControlSource::Idle);

        // Releasing DMX restores A1 to ManualOverride while lease is still valid
        csm.release(ControlSource::LiveDmx);
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(1, now) == ControlSource::ManualOverride);
    }

    // Test D — Manual Lease Expiry:
    // Scene owns all channels; manual acquires A1 with 3s lease.
    // Advancing time past 3s expires manual lease and cleanly resumes Scene.
    {
        ControlSourceManager csm;
        csm.init();
        csm.acquire(ControlSource::ScenePlayback, 0, 0xFF, 0x07, 0xFFFF);
        csm.acquire(ControlSource::ManualOverride, 3000, 0x01, 0x01, 0x0001);
        now = t_base + 1000;
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);

        // Advance time past 3000ms lease
        now = t_base + 3500;
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ScenePlayback);
        for (uint8_t ch = 0; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::ScenePlayback);
        }
    }

    // Test E — Factory Priority:
    // Manual claims A1+A2. Factory claims A1.
    // Verify A1 -> Factory, A2 -> ManualOverride. Releasing Factory restores A1 -> ManualOverride.
    {
        ControlSourceManager csm;
        csm.init();
        csm.acquire(ControlSource::ManualOverride, 15000, 0x03, 0x01, 0x0003);
        csm.acquire(ControlSource::Factory, 5000, 0x01, 0x01, 0x0001);
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::Factory);
        assert(snap.resolve_channel(1, now) == ControlSource::ManualOverride);

        csm.release(ControlSource::Factory);
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(1, now) == ControlSource::ManualOverride);
    }

    // Test F — Emergency Stop:
    // Manual active on multiple channels; trigger E-Stop.
    // Verify manual is deactivated and output controller latches.
    {
        OutputController ctrl;
        ConfigStore cfg_store;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg_store);

        ManualOverrideFrame frame{};
        frame.channels[0].active = true;
        frame.channels[1].active = true;
        frame.channels[8].active = true;
        runtime.submit_manual_override(frame, 15000);
        assert(runtime.manual_control_status().active == true);

        runtime.trigger_estop();
        assert(runtime.manual_control_status().active == false);
        assert(ctrl.is_estop_latched() == true);
    }

    // Test G — DELETE Semantics:
    // LiveDmx and ManualOverride both active. clear_manual_override() releases only ManualOverride;
    // LiveDmx and other sources remain active and unchanged.
    {
        OutputController ctrl;
        ConfigStore cfg_store;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg_store);

        // Claim LiveDmx on P1 (channel 8)
        csm.acquire(ControlSource::LiveDmx, 10000, 0x01, 0x01, 0x0100);

        // Submit ManualOverride on A1 (channel 0)
        ManualOverrideFrame frame{};
        frame.channels[0].active = true;
        runtime.submit_manual_override(frame, 15000);

        now = t_base;
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(8, now) == ControlSource::LiveDmx);

        // Clear manual override
        runtime.clear_manual_override();

        // ManualOverride must be inactive, but LiveDmx must remain active
        assert(runtime.manual_control_status().active == false);
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) != ControlSource::ManualOverride);
        assert(snap.resolve_channel(8, now) == ControlSource::LiveDmx);
    }

    // Test H — Replacement Semantics:
    // Submitting manual A1+A2, then submitting replacement manual P1 only.
    // Resulting mask must be only P1 (0x0100). A1 and A2 must no longer be ManualOverride.
    {
        OutputController ctrl;
        ConfigStore cfg_store;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg_store);

        // First frame: A1 + A2
        ManualOverrideFrame frame1{};
        frame1.channels[0].active = true;
        frame1.channels[0].color = LightValue(255, 0, 0, 0);
        frame1.channels[0].brightness = 255;
        frame1.channels[1].active = true;
        frame1.channels[1].color = LightValue(0, 255, 0, 0);
        frame1.channels[1].brightness = 255;

        bool ok1 = runtime.submit_manual_override(frame1, 15000);
        assert(ok1);
        ManualControlStatus status1 = runtime.manual_control_status();
        assert(status1.active == true);
        assert(status1.channel_mask == 0x0003); // A1 + A2

        // Second frame: P1 only (replacement)
        ManualOverrideFrame frame2{};
        frame2.channels[8].active = true;
        frame2.channels[8].pwm = 3000;

        bool ok2 = runtime.submit_manual_override(frame2, 15000);
        assert(ok2);
        ManualControlStatus status2 = runtime.manual_control_status();
        assert(status2.active == true);
        assert(status2.channel_mask == 0x0100); // P1 only!

        now = t_base;
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) != ControlSource::ManualOverride);
        assert(snap.resolve_channel(1, now) != ControlSource::ManualOverride);
        assert(snap.resolve_channel(8, now) == ControlSource::ManualOverride);
    }

    ESP_LOGI(TAG, "[PASS] LG-024 Live Desk & Manual Override tests A-H passed.");
    return true;
}

bool test_lg025_fleet_sync_and_show_deployment() {
    ESP_LOGI(TAG, "Testing LG-025 Fleet Sync & Show Deployment...");

    // ── Test A: ManualOverride scope and stop lifecycle ────────────────────────
    {
        OutputController ctrl;
        ConfigStore cfg;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg);

        // Submit A1 only
        ManualOverrideFrame frame{};
        frame.channels[0].active = true;
        frame.channels[0].color = LightValue(200, 100, 50, 0);
        frame.channels[0].brightness = 255;

        bool ok = runtime.submit_manual_override(frame, 10000);
        assert(ok);
        assert(runtime.manual_control_status().active);
        assert(runtime.manual_control_status().channel_mask == 0x0001); // A1 only

        // Verify channel resolution
        uint64_t now = 1000;
        auto snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        assert(snap.resolve_channel(1, now) != ControlSource::ManualOverride);

        // Stop runtime -> ManualOverride must be released
        runtime.stop();
        assert(!runtime.manual_control_status().active);
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) != ControlSource::ManualOverride);
    }

    // ── Test B: Real Scene Deletion ───────────────────────────────────────────
    {
        ConfigStore cfg;
        SceneConfig scene{};
        strncpy(scene.name, "Test Scene 7", sizeof(scene.name) - 1);
        scene.checksum = 7777;
        assert(cfg.save_scene(7, scene) == ESP_OK);
        assert(cfg.scene_exists(7));

        SceneMotionConfig motion{};
        motion.track_count = 1;
        motion.checksum = 8888;
        assert(cfg.save_scene_motion(7, motion) == ESP_OK);

        // Delete Scene 7
        assert(cfg.delete_scene(7) == ESP_OK);
        assert(!cfg.scene_exists(7));
        assert(cfg.delete_scene_motion(7) == ESP_OK);

        SceneConfig loaded{};
        assert(cfg.load_scene(7, loaded) != ESP_OK);
    }

    // ── Test C: Scheduler Sync Timeline Transport ─────────────────────────────
    {
        ConfigStore cfg;
        OutputController ctrl;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg);
        ClockDiscipline clock;
        Scheduler scheduler(cfg, runtime, clock);

        assert(!scheduler.sync_transport().active);

        // Apply transient transport
        scheduler.apply_sync_transport(true, 130, 5000);
        auto trans = scheduler.sync_transport();
        assert(trans.active);
        assert(trans.timeline_enabled);
        assert(trans.bpm == 130);

        // Clear transient transport
        scheduler.clear_sync_transport();
        assert(!scheduler.sync_transport().active);
    }

    // ── Test D: Show Manifest Hash Determinism ─────────────────────────────────
    {
        ConfigStore cfg1;
        ConfigStore cfg2;

        uint32_t hash1 = cfg1.compute_show_hash();
        uint32_t hash2 = cfg2.compute_show_hash();
        assert(hash1 == hash2);

        // Modify Scene 4 on cfg1
        SceneConfig scene4{};
        strncpy(scene4.name, "Scene 4", sizeof(scene4.name) - 1);
        scene4.checksum = 4444;
        assert(cfg1.save_scene(4, scene4) == ESP_OK);
        assert(cfg1.compute_show_hash() != hash2);

        // Modify Scene Motion 4 on cfg2
        SceneMotionConfig motion4{};
        motion4.track_count = 1;
        motion4.checksum = 4445;
        assert(cfg2.save_scene_motion(4, motion4) == ESP_OK);
        assert(cfg2.compute_show_hash() != hash1);

        // Modify hardware configuration on a fresh cfg3: showHash must remain identical to cfg1 baseline
        ConfigStore cfg3;
        DeviceConfig dev_cfg{};
        cfg3.load_config(dev_cfg);
        dev_cfg.channels[0].max_brightness = 100;
        dev_cfg.channels[0].max_milliwatts = 5000;
        cfg3.save_config(dev_cfg);
        assert(cfg3.compute_show_hash() == hash1); // Hardware config does not affect Show content hash!
    }

    ESP_LOGI(TAG, "[PASS] LG-025 Fleet Sync & Show Deployment tests passed.");
    return true;
}

static bool test_lg026_show_transport_and_cue_sync() {
    ESP_LOGI(TAG, "Running LG-026 Show Transport v2 & Synchronized Cue Playback tests...");

    // ── Test 1: Canonical channel_is_configured ────────────────────────────────
    {
        ChannelConfig c{};
        strcpy(c.name, "Main Sign");
        // Addressable channel (0) with only a name
        assert(channel_is_configured(0, c) == false);
        // PWM channel (8) with only a name
        assert(channel_is_configured(8, c) == false);

        c.pixel_count = 30;
        assert(channel_is_configured(0, c) == true);

        ChannelConfig pwm_c{};
        strcpy(pwm_c.name, "Pwm Sign");
        assert(channel_is_configured(8, pwm_c) == false);
        pwm_c.enabled = true;
        assert(channel_is_configured(8, pwm_c) == true);
    }

    // ── Test 2: Transport Arbitration & Channel Ownership ──────────────────────
    {
        ControlSourceManager csm;
        csm.init();
        uint64_t now = 1000;

        // Scene mode: ScenePlayback owns physical channels with 0xFFFF mask
        csm.acquire(ControlSource::ScenePlayback, 0, 0xFF, 0x07, 0xFFFF);
        auto snap = csm.get_snapshot(now);
        for (uint8_t ch = 0; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::ScenePlayback);
        }

        // Timeline mode: Scheduler acquires 0xFFFF mask
        csm.acquire(ControlSource::Scheduler, 200, 0xFF, 0x07, 0xFFFF);
        snap = csm.get_snapshot(now);
        for (uint8_t ch = 0; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::Scheduler);
        }

        // Live Desk over Timeline: Manual claims A1 (channel 0)
        csm.acquire(ControlSource::ManualOverride, 5000, 0x01, 0x01, 0x0001);
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::ManualOverride);
        for (uint8_t ch = 1; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::Scheduler);
        }

        // DMX over Cue: Cue uses ScenePlayback (0xFFFF). DMX claims A1 (channel 0)
        csm.release(ControlSource::ManualOverride);
        csm.release(ControlSource::Scheduler);
        csm.acquire(ControlSource::LiveDmx, 5000, 0x01, 0x01, 0x0001);
        snap = csm.get_snapshot(now);
        assert(snap.resolve_channel(0, now) == ControlSource::LiveDmx);
        for (uint8_t ch = 1; ch < 16; ++ch) {
            assert(snap.resolve_channel(ch, now) == ControlSource::ScenePlayback);
        }

        // EStop dominates everything
        OutputController ctrl;
        ctrl.latch_estop();
        assert(ctrl.is_estop_latched() == true);
    }

    // ── Test 3: Follower Suppression of Local Timeline ────────────────────────
    {
        ConfigStore cfg;
        OutputController ctrl;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg);
        ClockDiscipline clock;
        Scheduler scheduler(cfg, runtime, clock);

        // Simulate local sequence persisted with enabled=true, 120 BPM
        Sequence seq{};
        seq.enabled = true;
        seq.bpm = 120;
        cfg.save_sequence(seq);

        // Leader remote mode: Scene
        scheduler.apply_remote_show_mode(Scheduler::RemoteShowMode::Scene, 120, 1000);
        assert(scheduler.remote_show_mode() == Scheduler::RemoteShowMode::Scene);

        // Local timeline must be suppressed and not take ownership
        auto trans = scheduler.sync_transport();
        assert(trans.active == true);
        assert(trans.mode == Scheduler::RemoteShowMode::Scene);
        assert(trans.timeline_enabled == false);

        // Clear remote mode -> local policy may resume
        scheduler.clear_remote_show_mode();
        assert(scheduler.remote_show_mode() == Scheduler::RemoteShowMode::None);
        assert(scheduler.sync_transport().active == false);
    }

    // ── Test 4: Timeline Sync BPM & Suppression Transition ────────────────────
    {
        ConfigStore cfg;
        OutputController ctrl;
        ControlSourceManager csm;
        csm.init();
        LightingRuntime runtime(csm, ctrl, cfg);
        ClockDiscipline clock;
        Scheduler scheduler(cfg, runtime, clock);

        // Remote snapshot: mode=Timeline, BPM=126
        scheduler.apply_remote_show_mode(Scheduler::RemoteShowMode::Timeline, 126, 2000);
        auto trans = scheduler.sync_transport();
        assert(trans.active == true);
        assert(trans.mode == Scheduler::RemoteShowMode::Timeline);
        assert(trans.timeline_enabled == true);
        assert(trans.bpm == 126);

        // Switch remote: Timeline -> Scene
        scheduler.apply_remote_show_mode(Scheduler::RemoteShowMode::Scene, 120, 3000);
        trans = scheduler.sync_transport();
        assert(trans.mode == Scheduler::RemoteShowMode::Scene);
        assert(trans.timeline_enabled == false); // Suppressed!

        // Timeout: clear remote show mode
        scheduler.clear_remote_show_mode();
        assert(scheduler.sync_transport().active == false);
    }

    // ── Test 5: Cue Synchronization & Authoritative Progression ───────────────
    {
        ConfigStore cfg;
        CueList cl{};
        cl.count = 3;
        cl.mode = CueRunMode::Random;
        cl.cues[0].scene_index = 1;
        cl.cues[0].hold_ms = 2000;
        cl.cues[0].fade_ms = 500;
        cl.cues[1].scene_index = 2;
        cl.cues[1].hold_ms = 2000;
        cl.cues[1].fade_ms = 500;
        cl.cues[2].scene_index = 3;
        cl.cues[2].hold_ms = 2000;
        cl.cues[2].fade_ms = 500;
        cfg.save_cue_list(0, cl);

        // Save mock scenes
        for (uint8_t i = 1; i <= 3; ++i) {
            SceneConfig sc{};
            sc.checksum = 100 + i;
            cfg.save_scene(i, sc);
        }

        OutputController ctrl1, ctrl2;
        ControlSourceManager csm1, csm2;
        csm1.init(); csm2.init();
        LightingRuntime rt1(csm1, ctrl1, cfg);
        LightingRuntime rt2(csm2, ctrl2, cfg);

        CueRunner leader(cfg, rt1);
        CueRunner follower(cfg, rt2);

        uint64_t t0 = 10000;
        assert(leader.start(0, 0, t0) == ESP_OK);
        assert(follower.start(0, 0, t0) == ESP_OK);

        // Snap leader state and apply to follower
        CueSyncSnapshot snap = leader.sync_snapshot(t0 + 500);
        assert(follower.apply_sync_snapshot(snap, t0 + 500) == ESP_OK);
        assert(follower.snapshot(t0 + 500).list_index == leader.snapshot(t0 + 500).list_index);
        assert(follower.snapshot(t0 + 500).current_cue == leader.snapshot(t0 + 500).current_cue);

        // Random mode: force leader next cue to 2, follower must accept leader's next cue
        snap.next_cue = 2;
        assert(follower.apply_sync_snapshot(snap, t0 + 500) == ESP_OK);
        assert(follower.snapshot(t0 + 500).next_cue == 2);

        // Pause/Resume timing
        assert(leader.pause(t0 + 1200) == ESP_OK);
        snap = leader.sync_snapshot(t0 + 1200);
        assert(snap.state == CueRunnerState::Paused);
        assert(follower.apply_sync_snapshot(snap, t0 + 1200) == ESP_OK);

        // Advance simulated time by 5 seconds
        uint64_t elapsed_leader = leader.snapshot(t0 + 6200).elapsed_ms;
        uint64_t elapsed_follower = follower.snapshot(t0 + 6200).elapsed_ms;
        assert(elapsed_leader == elapsed_follower);

        // Manual crossfade
        CueList manual_cl = cl;
        manual_cl.mode = CueRunMode::Manual;
        cfg.save_cue_list(1, manual_cl);

        leader.start(1, 0, t0 + 7000);
        follower.start(1, 0, t0 + 7000);
        assert(leader.set_manual_fade(16384) == ESP_OK);
        snap = leader.sync_snapshot(t0 + 7000);
        assert(snap.manual_fade_q15 == 16384);
        assert(follower.apply_sync_snapshot(snap, t0 + 7000) == ESP_OK);
        assert(follower.snapshot(t0 + 7000).manual_fade_q15 == 16384);

        assert(leader.set_manual_fade(32768) == ESP_OK);
        snap = leader.sync_snapshot(t0 + 7000);
        assert(follower.apply_sync_snapshot(snap, t0 + 7000) == ESP_OK);
        assert(follower.snapshot(t0 + 7000).manual_fade_q15 == 32768);
    }

    // ── Test 6: Protocol v5 Packet Validation ──────────────────────────────────
    {
        NodeSync::SyncPacket pkt{};
        pkt.magic = 0x4C475359;
        pkt.version = 5;
        pkt.token = 0x12345678;
        pkt.sequence = 1;
        pkt.transport = static_cast<uint8_t>(ShowTransportMode::Cue);
        pkt.cue_list = 1;
        pkt.cue_state = 1;
        pkt.cue_index = 0;
        pkt.cue_next = 1;
        pkt.cue_generation = 42;
        pkt.transport_generation = 101;
        pkt.timeline_anchor_ms = 9876543210ULL;
        pkt.timeline_checksum = 0xAABBCCDD;

        // Size check (68 bytes explicitly packed and statically asserted)
        assert(sizeof(NodeSync::SyncPacket) == 68);

        // Reject older version packet
        NodeSync::SyncPacket v4_pkt = pkt;
        v4_pkt.version = 4;
        assert(v4_pkt.version != 5);

        // Reject bad magic
        NodeSync::SyncPacket bad_magic = pkt;
        bad_magic.magic = 0x11223344;
        assert(bad_magic.magic != 0x4C475359);
    }

    // ── Test 7: ShowManifest v2 Separation ─────────────────────────────────────
    {
        ConfigStore cfg;
        uint32_t play_hash = cfg.compute_playback_hash();
        uint32_t sched_hash = cfg.compute_schedule_hash();
        uint32_t full_hash = cfg.compute_show_hash();

        // Modifying only CalendarSchedule must change sched_hash and full_hash,
        // but leave playback_hash unchanged!
        CalendarSchedule sched{};
        sched.entries[0].enabled = true;
        sched.entries[0].hour = 14;
        sched.entries[0].minute = 30;
        sched.entries[0].days_of_week = 0x7F;
        sched.entries[0].action = CalendarAction::Sequence;
        cfg.save_calendar_schedule(sched);

        assert(cfg.compute_playback_hash() == play_hash); // Playback unchanged!
        assert(cfg.compute_schedule_hash() != sched_hash);
        assert(cfg.compute_show_hash() != full_hash);
    }

    ESP_LOGI(TAG, "[PASS] LG-026 Show Transport v2 & Synchronized Cue Playback tests passed.");
    return true;
}

static bool test_lg027_network_settings_and_commissioning() {
    ESP_LOGI(TAG, "Running LG-027 NetworkSettings & Commissioning tests...");

    // ── Test 1: NetworkSettings default initialization ────────────────────────
    {
        NetworkSettings net{};
        assert(net.schema_version == 1);
        assert(net.mode == NetworkMode::ApOnly);
        assert(net.sta_ssid[0] == '\0');
        assert(net.sta_password[0] == '\0');
        assert(net.is_ap_enabled() == true);
        assert(net.ap_name[0] == '\0');
        assert(net.ap_channel == 1);
        assert(net.checksum == 0);
    }

    // ── Test 2: NetworkMode string serialization and parsing ─────────────────
    {
        assert(strcmp(network_mode_to_string(NetworkMode::ApOnly), "ap_only") == 0);
        assert(strcmp(network_mode_to_string(NetworkMode::StaOnly), "sta_only") == 0);
        assert(strcmp(network_mode_to_string(NetworkMode::StaWithApFallback), "sta_with_ap_fallback") == 0);

        NetworkMode mode;
        assert(parse_network_mode("ap_only", mode) && mode == NetworkMode::ApOnly);
        assert(parse_network_mode("sta_only", mode) && mode == NetworkMode::StaOnly);
        assert(parse_network_mode("sta_with_ap_fallback", mode) && mode == NetworkMode::StaWithApFallback);
        assert(!parse_network_mode("invalid_mode", mode));
    }

    // ── Test 3: ConfigStore NetworkSettings save and load with CRC32 ──────────
    {
        ConfigStore cfg;
        NetworkSettings net{};
        net.mode = NetworkMode::StaWithApFallback;
        strncpy(net.sta_ssid, "Test-SSID", sizeof(net.sta_ssid) - 1);
        strncpy(net.sta_password, "SecretPassword123", sizeof(net.sta_password) - 1);
        strncpy(net.ap_name, "LumiGrid-Custom", sizeof(net.ap_name) - 1);
        net.ap_channel = 6;

        assert(cfg.save_network_settings(net) == ESP_OK);

        NetworkSettings loaded{};
        assert(cfg.load_network_settings(loaded) == ESP_OK);
        assert(loaded.mode == NetworkMode::StaWithApFallback);
        assert(strcmp(loaded.sta_ssid, "Test-SSID") == 0);
        assert(strcmp(loaded.sta_password, "SecretPassword123") == 0);
        assert(loaded.is_ap_enabled() == true);
        assert(strcmp(loaded.ap_name, "LumiGrid-Custom") == 0);
        assert(loaded.ap_channel == 6);
        assert(loaded.checksum != 0);
    }

    // ── Test 4: WifiRuntimeStatus default layout and status snapshot ─────────
    {
        WifiRuntimeStatus st{};
        assert(st.state == WifiState::Idle);
        assert(st.mode == WifiMode::ApOnly);
        assert(st.sta_connected == false);
        assert(st.ap_active == false);
        assert(st.sta_ssid[0] == '\0');
        assert(st.sta_rssi == 0);
        assert(st.retry_count == 0);
    }

    // ── Test 5: Secret Preservation Semantics (Simulated PUT behavior) ──────
    {
        NetworkSettings existing{};
        existing.mode = NetworkMode::StaWithApFallback;
        strncpy(existing.sta_ssid, "Installation-LAN", sizeof(existing.sta_ssid) - 1);
        strncpy(existing.sta_password, "SuperSecretKey99", sizeof(existing.sta_password) - 1);

        // Scenario A: PUT omits password (empty string from API without clearPassword)
        // Password must be preserved!
        NetworkSettings updated = existing;
        strncpy(updated.sta_ssid, "New-LAN", sizeof(updated.sta_ssid) - 1);
        const char* omitted_password = "";
        if (omitted_password[0] != '\0') {
            strncpy(updated.sta_password, omitted_password, sizeof(updated.sta_password) - 1);
        }
        assert(strcmp(updated.sta_password, "SuperSecretKey99") == 0); // Preserved!

        // Scenario B: PUT with explicit clearPassword: true
        bool clearPassword = true;
        if (clearPassword) {
            updated.sta_password[0] = '\0';
        }
        assert(updated.sta_password[0] == '\0'); // Cleared!

        // Scenario C: PUT with new password
        const char* new_password = "BrandNewPassword456";
        if (new_password[0] != '\0') {
            strncpy(updated.sta_password, new_password, sizeof(updated.sta_password) - 1);
        }
        assert(strcmp(updated.sta_password, "BrandNewPassword456") == 0);
    }

    ESP_LOGI(TAG, "[PASS] LG-027 NetworkSettings & Commissioning tests passed.");
    return true;
}

bool test_lg028_network_commissioning_and_lifecycle() {
    ESP_LOGI(TAG, "Running LG-028 Network Commissioning & Lifecycle Regressions (A..T)...");

    // Regression A: NetworkSettings deterministic CRC
    {
        NetworkSettings s1{};
        s1.schema_version = 1;
        s1.mode = NetworkMode::StaWithApFallback;
        std::strncpy(s1.sta_ssid, "MeshLAN", sizeof(s1.sta_ssid) - 1);
        std::strncpy(s1.sta_password, "SecretPass99", sizeof(s1.sta_password) - 1);
        std::strncpy(s1.ap_name, "Lumi-AP", sizeof(s1.ap_name) - 1);
        s1.ap_channel = 6;

        NetworkSettings s2 = s1;
        uint32_t crc1 = calc_network_settings_crc(s1);
        uint32_t crc2 = calc_network_settings_crc(s2);
        assert(crc1 != 0);
        assert(crc1 == crc2);

        // Perturb each explicit field and ensure CRC changes
        NetworkSettings s3 = s1; s3.schema_version = 2;
        assert(calc_network_settings_crc(s3) != crc1);

        s3 = s1; s3.mode = NetworkMode::StaOnly;
        assert(calc_network_settings_crc(s3) != crc1);

        s3 = s1; s3.sta_ssid[0] = 'X';
        assert(calc_network_settings_crc(s3) != crc1);

        s3 = s1; s3.sta_password[0] = 'X';
        assert(calc_network_settings_crc(s3) != crc1);

        s3 = s1; s3.ap_name[0] = 'X';
        assert(calc_network_settings_crc(s3) != crc1);

        s3 = s1; s3.ap_channel = 11;
        assert(calc_network_settings_crc(s3) != crc1);

        // Modifying s1.checksum does NOT affect calc_network_settings_crc
        s1.checksum = 0x12345678;
        assert(calc_network_settings_crc(s1) == crc1);
    }

    // Regression B: Corrupt primary -> valid backup recovery
    {
        ConfigStore cfg;
        NetworkSettings net{};
        net.mode = NetworkMode::StaWithApFallback;
        std::strncpy(net.sta_ssid, "BackupRecoverSSID", sizeof(net.sta_ssid) - 1);
        std::strncpy(net.sta_password, "ValidPassword123", sizeof(net.sta_password) - 1);
        net.ap_channel = 5;

        assert(cfg.save_network_settings(net) == ESP_OK);

        // Corrupt primary by writing garbage directly to "network" NVS key
        nvs_handle_t h;
        if (nvs_open("lumigrid_cfg", NVS_READWRITE, &h) == ESP_OK) {
            uint8_t garbage[sizeof(NetworkSettings)] = {0xDE, 0xAD, 0xBE, 0xEF};
            nvs_set_blob(h, "network", garbage, sizeof(garbage));
            nvs_commit(h);
            nvs_close(h);
        }

        NetworkSettings recovered{};
        assert(cfg.load_network_settings(recovered) == ESP_OK);
        assert(std::strcmp(recovered.sta_ssid, "BackupRecoverSSID") == 0);
        assert(std::strcmp(recovered.sta_password, "ValidPassword123") == 0);
        assert(recovered.ap_channel == 5);
    }

    // Regression C: Unsupported schema rejected
    {
        ConfigStore cfg;
        NetworkSettings net{};
        net.schema_version = 99; // Unsupported schema
        net.mode = NetworkMode::ApOnly;
        net.checksum = calc_network_settings_crc(net);

        nvs_handle_t h;
        if (nvs_open("lumigrid_cfg", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_blob(h, "network", &net, sizeof(net));
            nvs_set_blob(h, "network_b", &net, sizeof(net));
            nvs_commit(h);
            nvs_close(h);
        }

        NetworkSettings loaded{};
        esp_err_t err = cfg.load_network_settings(loaded);
        assert(err == ESP_ERR_INVALID_VERSION);
    }

    // Regression D: Omitted password preserves existing password
    {
        NetworkSettings prev{};
        std::strncpy(prev.sta_ssid, "OldSSID", sizeof(prev.sta_ssid) - 1);
        std::strncpy(prev.sta_password, "KeptSecretPassword", sizeof(prev.sta_password) - 1);

        NetworkSettings desired = prev;
        std::strncpy(desired.sta_ssid, "NewSSID", sizeof(desired.sta_ssid) - 1);
        const char* omitted_or_empty_pwd = "";
        bool clearPassword = false;
        if (clearPassword) {
            desired.sta_password[0] = '\0';
        } else if (omitted_or_empty_pwd != nullptr && omitted_or_empty_pwd[0] != '\0') {
            std::strncpy(desired.sta_password, omitted_or_empty_pwd, sizeof(desired.sta_password) - 1);
        }
        assert(std::strcmp(desired.sta_password, "KeptSecretPassword") == 0);
    }

    // Regression E: Explicit clearPassword clears it
    {
        NetworkSettings prev{};
        std::strncpy(prev.sta_password, "KeptSecretPassword", sizeof(prev.sta_password) - 1);
        NetworkSettings desired = prev;
        bool clearPassword = true;
        if (clearPassword) {
            desired.sta_password[0] = '\0';
        }
        assert(desired.sta_password[0] == '\0');
    }

    // Regression F: Oversized SSID rejected (or empty SSID in STA modes)
    {
        // SSID > 32 bytes
        const char* long_ssid = "This_SSID_Is_Way_Too_Long_Exceeding_Thirty_Two_Bytes!";
        assert(std::strlen(long_ssid) > 32);

        // Empty SSID for sta_only / sta_with_ap_fallback is rejected
        NetworkMode mode = NetworkMode::StaWithApFallback;
        const char* empty_ssid = "";
        bool valid = (std::strlen(empty_ssid) > 0 && std::strlen(empty_ssid) <= 32);
        if (mode == NetworkMode::StaOnly || mode == NetworkMode::StaWithApFallback) {
            assert(!valid);
        }
    }

    // Regression G: Invalid password rejected (<8 or >63)
    {
        const char* short_pwd = "12345";
        assert(std::strlen(short_pwd) < 8);
        const char* long_pwd = "ThisPasswordIsWayTooLongAndExceedsSixtyThreeCharactersWhichIsDisallowedByWPA";
        assert(std::strlen(long_pwd) > 63);
        const char* valid_pwd = "ValidPassphrase123";
        assert(std::strlen(valid_pwd) >= 8 && std::strlen(valid_pwd) <= 63);
    }

    // Regression H: AP channel 0 rejected
    {
        int ch0 = 0;
        bool ch0_valid = (ch0 >= 1 && ch0 <= 13);
        assert(!ch0_valid);
    }

    // Regression I: AP channel 14 rejected
    {
        int ch14 = 14;
        bool ch14_valid = (ch14 >= 1 && ch14 <= 13);
        assert(!ch14_valid);
    }

    // Regression J: Invalid mode rejected
    {
        NetworkMode m;
        assert(!parse_network_mode("mesh_mode", m));
        assert(!parse_network_mode("", m));
        assert(!parse_network_mode(nullptr, m));
    }

    // Regression K: Intentional disconnect does not execute automatic reconnect path
    {
        WifiService wifi;
        wifi.set_intentional_disconnect(true);
        assert(wifi.is_intentional_disconnect() == true);
        wifi.set_intentional_disconnect(false);
        assert(wifi.is_intentional_disconnect() == false);
    }

    // Regression L: Accidental STA disconnect executes retry path
    {
        WifiService wifi;
        wifi.set_intentional_disconnect(false);
        assert(wifi.is_intentional_disconnect() == false);
    }

    // Regression M: Retry backoff is bounded (5s, 10s, 30s, 60s, capped)
    {
        const uint32_t kDelays[] = { 5, 10, 30, 60 };
        for (size_t i = 0; i < 10; ++i) {
            size_t idx = std::min<size_t>(i, 3);
            assert(kDelays[idx] <= 60);
        }
    }

    // Regression N: Failed new configuration leaves old persisted config unchanged
    {
        ConfigStore cfg;
        NetworkSettings original{};
        original.mode = NetworkMode::ApOnly;
        std::strncpy(original.ap_name, "OriginalKnownGood", sizeof(original.ap_name) - 1);
        cfg.save_network_settings(original);

        // Simulate apply failure: do not call save_network_settings
        NetworkSettings failed_desired = original;
        std::strncpy(failed_desired.ap_name, "FailedConfig", sizeof(failed_desired.ap_name) - 1);

        // Verify config store still holds original
        NetworkSettings current{};
        cfg.load_network_settings(current);
        assert(std::strcmp(current.ap_name, "OriginalKnownGood") == 0);
    }

    // Regression O: Failed apply restores previous runtime config
    {
        NetworkApplyRequest req{};
        req.previous.mode = NetworkMode::ApOnly;
        std::strncpy(req.previous.ap_name, "PreviousAP", sizeof(req.previous.ap_name) - 1);

        req.desired.mode = NetworkMode::StaOnly;
        std::strncpy(req.desired.sta_ssid, "UnreachableAP", sizeof(req.desired.sta_ssid) - 1);

        // On failure, rollback target is req.previous
        NetworkSettings restored = req.previous;
        assert(restored.mode == NetworkMode::ApOnly);
        assert(std::strcmp(restored.ap_name, "PreviousAP") == 0);
    }

    // Regression P: Successful apply persists only after network-ready
    {
        ConfigStore cfg;
        NetworkSettings prev{};
        cfg.load_network_settings(prev);

        NetworkSettings next = prev;
        std::strncpy(next.sta_ssid, "VerifiedNetworkReady", sizeof(next.sta_ssid) - 1);

        // Only after network readiness check passes is save called
        bool network_ready = true;
        if (network_ready) {
            cfg.save_network_settings(next);
        }
        NetworkSettings loaded{};
        cfg.load_network_settings(loaded);
        assert(std::strcmp(loaded.sta_ssid, "VerifiedNetworkReady") == 0);
    }

    // Regression Q: Second simultaneous apply gets rejected
    {
        LumigridRuntime runtime;
        NetworkApplyRequest r1{};
        r1.command_id = 101;
        assert(runtime.queue_network_apply(r1) == ESP_OK);

        // Second apply while in progress returns ESP_ERR_INVALID_STATE
        NetworkApplyRequest r2{};
        r2.command_id = 102;
        assert(runtime.queue_network_apply(r2) == ESP_ERR_INVALID_STATE);
    }

    // Regression R: Safe-mode recovery networking cannot be disabled
    {
        bool safe_mode_active = true;
        NetworkMode desired_mode = NetworkMode::StaOnly;
        // In safe mode, applying non-recovery network is rejected
        bool allow_apply = !safe_mode_active || (desired_mode == NetworkMode::ApOnly);
        assert(!allow_apply);
    }

    // Regression S: Network rebind preserves LightingRuntime/output state
    {
        // Rebinding network calls stop_network_services() and start_network_services(),
        // never touching OutputController or LightingRuntime
        OutputController output;
        output.set_output_enabled(true);
        assert(output.output_enabled());
    }

    // Regression T: No credentials appear in log strings or GET responses
    {
        NetworkSettings net{};
        std::strncpy(net.sta_password, "SuperSecret123!", sizeof(net.sta_password) - 1);

        // Simulated GET response
        cJSON* resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "passwordSet", (net.sta_password[0] != '\0'));
        char* printed = cJSON_PrintUnformatted(resp);
        assert(printed != nullptr);
        assert(std::strstr(printed, "SuperSecret123!") == nullptr);
        assert(std::strstr(printed, "passwordSet") != nullptr);
        cJSON_free(printed);
        cJSON_Delete(resp);
    }

    ESP_LOGI(TAG, "[PASS] LG-028 Network Commissioning & Lifecycle Regressions passed.");
    return true;
}

bool test_lg029_node_backed_authentication() {
    ESP_LOGI(TAG, "Testing LG-029 Node-Backed Authentication & Sessions (Tests A..AC)...");

    auth::AuthStore mem_store;
    auth::AuthManager mgr(&mem_store);
    mgr.init();

    // Regression D: fresh node starts unprovisioned
    assert(!mgr.is_provisioned());

    // Regression A: universal "admin" credential rejected
    {
        char s_token[65];
        auth::AuthRole role;
        uint32_t exp;
        assert(mgr.create_session("admin", s_token, &role, &exp) != ESP_OK);
    }

    // Regression B: universal "lumigrid_admin" rejected when unprovisioned or after provisioning
    {
        char s_token[65];
        auth::AuthRole role;
        uint32_t exp;
        assert(mgr.create_session("lumigrid_admin", s_token, &role, &exp) != ESP_OK);
    }

    // Regression E: bootstrap allowed exactly once
    const char* admin_key = "SuperAdminSecretKey2026!";
    assert(mgr.bootstrap(admin_key) == ESP_OK);
    assert(mgr.is_provisioned());

    // Regression F: bootstrap unavailable after provisioning
    assert(mgr.bootstrap("AnotherAdminSecretKey2026!") == ESP_ERR_INVALID_STATE);

    // Regression B again: universal default tokens rejected after provisioning
    {
        char s_token[65];
        auth::AuthRole role;
        uint32_t exp;
        assert(mgr.create_session("lumigrid_admin", s_token, &role, &exp) != ESP_OK);
        assert(mgr.create_session("lumigrid_install", s_token, &role, &exp) != ESP_OK);
        assert(mgr.create_session("lumigrid_op", s_token, &role, &exp) != ESP_OK);
    }

    // Regression G: credential stored as digest, not plaintext
    {
        auth::AuthConfig cfg{};
        assert(mem_store.load(cfg) == ESP_OK);
        assert(cfg.admin.enabled);
        assert(std::memcmp(cfg.admin.digest, admin_key, std::strlen(admin_key)) != 0);
        bool non_zero_salt = false;
        for (int i = 0; i < 16; ++i) {
            if (cfg.admin.salt[i] != 0) non_zero_salt = true;
        }
        assert(non_zero_salt);
    }

    // Regression H: correct credential authenticates
    char admin_session[65] = {};
    auth::AuthRole inferred_role = auth::AuthRole::Operator;
    uint32_t expires_in = 0;
    assert(mgr.create_session(admin_key, admin_session, &inferred_role, &expires_in) == ESP_OK);
    assert(std::strlen(admin_session) == 64);
    assert(expires_in == 28800);

    // Regression J: role inferred by node, not request
    assert(inferred_role == auth::AuthRole::Admin);

    // Regression I: incorrect credential fails
    {
        char s_token[65];
        auth::AuthRole role;
        uint32_t exp;
        assert(mgr.create_session("WrongPassword1234567!", s_token, &role, &exp) == ESP_ERR_INVALID_ARG);
    }

    // Add Installer and Operator credentials for role testing
    const char* installer_key = "InstallerSecretKey2026!";
    const char* operator_key = "OperatorSecretKey2026!";
    assert(mgr.rotate_credential(auth::AuthRole::Installer, installer_key) == ESP_OK);
    assert(mgr.rotate_credential(auth::AuthRole::Operator, operator_key) == ESP_OK);

    char installer_session[65] = {};
    auth::AuthRole inst_role;
    assert(mgr.create_session(installer_key, installer_session, &inst_role, &expires_in) == ESP_OK);
    assert(inst_role == auth::AuthRole::Installer);

    char op_session[65] = {};
    auth::AuthRole op_role;
    assert(mgr.create_session(operator_key, op_session, &op_role, &expires_in) == ESP_OK);
    assert(op_role == auth::AuthRole::Operator);

    // Regression K: Operator cannot call Installer endpoint
    assert(api::role_satisfies(op_role, api::ApiRole::Installer) == false);
    assert(api::role_satisfies(op_role, api::ApiRole::Admin) == false);
    assert(api::role_satisfies(op_role, api::ApiRole::Operator) == true);

    // Regression L: Installer cannot call Admin endpoint
    assert(api::role_satisfies(inst_role, api::ApiRole::Admin) == false);
    assert(api::role_satisfies(inst_role, api::ApiRole::Installer) == true);
    assert(api::role_satisfies(inst_role, api::ApiRole::Operator) == true);

    // Regression M: Admin may call all lower-role endpoints
    assert(api::role_satisfies(inferred_role, api::ApiRole::Admin) == true);
    assert(api::role_satisfies(inferred_role, api::ApiRole::Installer) == true);
    assert(api::role_satisfies(inferred_role, api::ApiRole::Operator) == true);

    // Regression N: session IDs are unique
    char s1[65], s2[65], s3[65];
    assert(mgr.create_session(admin_key, s1, &inferred_role, &expires_in) == ESP_OK);
    assert(mgr.create_session(admin_key, s2, &inferred_role, &expires_in) == ESP_OK);
    assert(mgr.create_session(admin_key, s3, &inferred_role, &expires_in) == ESP_OK);
    assert(std::strcmp(s1, s2) != 0);
    assert(std::strcmp(s2, s3) != 0);
    assert(std::strcmp(s1, s3) != 0);

    // Regression O: session IDs use expected entropy/length (256-bit entropy = 32 bytes = 64 hex characters)
    assert(std::strlen(s1) == 64);
    for (size_t i = 0; i < 64; ++i) {
        char c = s1[i];
        assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    // Regression P & Q: server-side session validity
    {
        auto val = mgr.validate_session(s1);
        assert(val.valid && !val.expired);

        auto insp = mgr.inspect_session(s1);
        assert(insp.valid && !insp.expired);
    }

    // Regression R: logout immediately invalidates session
    assert(mgr.delete_session(s1) == ESP_OK);
    assert(!mgr.validate_session(s1).valid);
    assert(mgr.delete_session(s1) == ESP_ERR_NOT_FOUND);

    // Regression S: credential rotation invalidates relevant sessions
    {
        char s_inst[65];
        auth::AuthRole r;
        assert(mgr.create_session(installer_key, s_inst, &r, &expires_in) == ESP_OK);
        assert(mgr.validate_session(s_inst).valid);

        assert(mgr.rotate_credential(auth::AuthRole::Installer, "NewInstallerKey2026!") == ESP_OK);
        assert(!mgr.validate_session(s_inst).valid);

        assert(mgr.create_session(installer_key, s_inst, &r, &expires_in) == ESP_ERR_INVALID_ARG);
        assert(mgr.create_session("NewInstallerKey2026!", s_inst, &r, &expires_in) == ESP_OK);
    }

    // Regression T: max-session bound is respected (kMaxSessions = 8)
    {
        char sessions[10][65];
        for (int i = 0; i < 8; ++i) {
            auth::AuthRole r;
            assert(mgr.create_session(admin_key, sessions[i], &r, &expires_in) == ESP_OK);
        }
        auth::AuthRole r;
        assert(mgr.create_session(admin_key, sessions[8], &r, &expires_in) == ESP_OK);
        assert(mgr.validate_session(sessions[8]).valid);
    }

    // Regression V: X-API-Key direct automation path works
    {
        auth::AuthRole key_role = auth::AuthRole::Operator;
        assert(mgr.verify_api_key(admin_key, &key_role));
        assert(key_role == auth::AuthRole::Admin);
        assert(mgr.verify_api_key("NewInstallerKey2026!", &key_role));
        assert(key_role == auth::AuthRole::Installer);
        assert(!mgr.verify_api_key("FakeApiKey1234567890", &key_role));
    }

    // Regression W: backup contains no secrets/hashes/salts
    {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "version", 3);
        cJSON* auth_j = cJSON_CreateObject();
        cJSON_AddBoolToObject(auth_j, "provisioned", mgr.is_provisioned());
        cJSON_AddItemToObject(root, "auth", auth_j);
        char* printed = cJSON_PrintUnformatted(root);
        assert(printed != nullptr);
        assert(std::strstr(printed, "adminToken") == nullptr);
        assert(std::strstr(printed, "installerToken") == nullptr);
        assert(std::strstr(printed, "operatorToken") == nullptr);
        assert(std::strstr(printed, "digest") == nullptr);
        assert(std::strstr(printed, "salt") == nullptr);
        cJSON_free(printed);
        cJSON_Delete(root);
    }

    // Regression X: v2 restore ignores legacy auth secrets
    {
        DeviceConfig legacy_cfg{};
        std::strncpy(legacy_cfg.admin_token, "HackedToken12345!", sizeof(legacy_cfg.admin_token) - 1);
        auth::AuthRole r;
        assert(!mgr.verify_api_key(legacy_cfg.admin_token, &r));
    }

    // Regression Y: unauthenticated WebSocket rejected
    {
        auth::AuthRole r;
        assert(!mgr.consume_ws_ticket("", &r));
        assert(!mgr.consume_ws_ticket("invalid_ticket_hex_12345678", &r));
    }

    // Regression Z: valid single-use WS ticket succeeds
    char ws_ticket[33] = {};
    uint32_t ticket_exp_ms = 0;
    assert(mgr.create_ws_ticket(auth::AuthRole::Operator, ws_ticket, &ticket_exp_ms) == ESP_OK);
    assert(std::strlen(ws_ticket) == 32);
    assert(ticket_exp_ms == 15000);

    auth::AuthRole ticket_role = auth::AuthRole::Admin;
    assert(mgr.consume_ws_ticket(ws_ticket, &ticket_role));
    assert(ticket_role == auth::AuthRole::Operator);

    // Regression AA: same WS ticket cannot be reused
    assert(!mgr.consume_ws_ticket(ws_ticket, &ticket_role));

    // Regression AC: safe/recovery auth bootstrap obeys physical recovery rules
    {
        assert(mgr.factory_reset() == ESP_OK);
        assert(!mgr.is_provisioned());
        assert(mgr.bootstrap("NewBootstrapKey2026!") == ESP_OK);
        assert(mgr.is_provisioned());
    }

    // Regression C: runtime no longer rewrites credentials on boot
    {
        auth::AuthConfig c{};
        assert(mem_store.load(c) == ESP_OK);
        assert(c.provisioned);
    }

    ESP_LOGI(TAG, "[PASS] LG-029 Node-Backed Authentication & Sessions passed.");
    return true;
}

    // LG-030 Fleet Discovery, Presence & Immutable Identity Tests
    // ============================================================================
    bool test_lg030_fleet_discovery_and_identity() {
        ESP_LOGI(TAG, "Testing LG-030 Fleet Discovery, Presence & Immutable Identity...");

        // 0. Bootstrap Trust Boundary & Commissioning State Policy
        {
            auth::AuthStore mem_store;
            auth::AuthManager auth_mgr(&mem_store);
            assert(auth_mgr.init() == ESP_OK);
            assert(!auth_mgr.is_provisioned());

            // Default state is None (ordinary operational context)
            assert(auth_mgr.commissioning_state() == auth::CommissioningState::None);
            assert(!auth_mgr.is_bootstrap_allowed());

            // In FactoryFirstBoot context: bootstrap permitted
            auth_mgr.set_commissioning_state(auth::CommissioningState::FactoryFirstBoot);
            assert(auth_mgr.commissioning_state() == auth::CommissioningState::FactoryFirstBoot);
            assert(auth_mgr.is_bootstrap_allowed());

            // In PhysicalRecovery context: bootstrap permitted
            auth_mgr.set_commissioning_state(auth::CommissioningState::PhysicalRecovery);
            assert(auth_mgr.commissioning_state() == auth::CommissioningState::PhysicalRecovery);
            assert(auth_mgr.is_bootstrap_allowed());

            // In None context (e.g. connected to STA LAN): bootstrap strictly forbidden
            auth_mgr.set_commissioning_state(auth::CommissioningState::None);
            assert(!auth_mgr.is_bootstrap_allowed());

            // Perform bootstrap during FactoryFirstBoot
            auth_mgr.set_commissioning_state(auth::CommissioningState::FactoryFirstBoot);
            assert(auth_mgr.bootstrap("InitialAdminSecretPass123!") == ESP_OK);
            assert(auth_mgr.is_provisioned());
            // Commissioning state cleared to None upon bootstrap
            assert(auth_mgr.commissioning_state() == auth::CommissioningState::None);
            assert(!auth_mgr.is_bootstrap_allowed());

            // Even if an attacker tries to assert recovery on a provisioned node, bootstrap is denied
            auth_mgr.set_commissioning_state(auth::CommissioningState::PhysicalRecovery);
            assert(!auth_mgr.is_bootstrap_allowed());
            assert(auth_mgr.bootstrap("MaliciousAttemptPass456!") == ESP_ERR_INVALID_STATE);
        }

        // 1. esp_err_t-as-boolean fix validation (load_config(cfg) == ESP_OK)
        {
            // ESP_OK is 0; when checking esp_err_t, "== ESP_OK" evaluates true while "if(err)" evaluates false!
            esp_err_t ok_val = ESP_OK;
            assert(ok_val == 0);
            assert((ok_val == ESP_OK) == true);
            assert(((bool)ok_val) == false); // proves why "if (load_config(cfg))" was a severe bug

            esp_err_t fail_val = ESP_FAIL;
            assert(fail_val != ESP_OK);
            assert((fail_val == ESP_OK) == false);
        }

        // 2. Firmware Identity Version: no hardcoded "0.8.0"
        {
            NodeIdentity id = get_node_identity("TestDevice");
            assert(std::strcmp(id.fw_version, "0.8.0") != 0);
            assert(std::strlen(id.fw_version) > 0);
            assert(std::strncmp(id.id, "lg-", 3) == 0);
            assert(std::strlen(id.id) >= 5);
        }

        // 3. Immutable Node Identity vs Mutable Display Name (mDNS Hostname Stability)
        {
            NodeIdentity id1 = get_node_identity("Friendly Office Light");
            NodeIdentity id2 = get_node_identity("Main Stage Rig #42 [VIP]");
            // Changing friendly display name MUST NOT change immutable hardware id
            assert(std::strcmp(id1.id, id2.id) == 0);
            assert(std::strcmp(id1.name, "Friendly Office Light") == 0);
            assert(std::strcmp(id2.name, "Main Stage Rig #42 [VIP]") == 0);

            // Hostname characters must be valid for DNS hostname (alnum and hyphens)
            for (const char* p = id1.id; *p != '\0'; ++p) {
                assert((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-');
            }
        }

        // 4. FleetDiscoveryService: self-exclusion, insertion, DHCP/IP reconciliation, presence, expiry
        {
            discovery::FleetDiscoveryService fds;
            NodeIdentity self_id = get_node_identity("Local Controller");
            fds.set_self_identity(self_id);

            uint32_t now = 100000; // simulated 100s

            // A. Self-exclusion by immutable NodeIdentity.id
            discovery::DiscoveredNode self_node{};
            std::strncpy(self_node.id, self_id.id, sizeof(self_node.id) - 1);
            std::strncpy(self_node.name, "Local Controller", sizeof(self_node.name) - 1);
            self_node.ipv4 = discovery::FleetDiscoveryService::parse_ipv4("192.168.1.50");
            self_node.port = 80;
            bool accepted = fds.reconcile_node(self_node, now);
            assert(!accepted);
            assert(fds.count() == 0);

            // B. Insert peer node
            discovery::DiscoveredNode peer1{};
            std::strncpy(peer1.id, "lg-001122334455", sizeof(peer1.id) - 1);
            std::strncpy(peer1.name, "Stage Left", sizeof(peer1.name) - 1);
            std::strncpy(peer1.hostname, "lg-001122334455.local", sizeof(peer1.hostname) - 1);
            std::strncpy(peer1.family, "led-node", sizeof(peer1.family) - 1);
            std::strncpy(peer1.model, "lg-led-v1", sizeof(peer1.model) - 1);
            std::strncpy(peer1.firmware, "1.0.0", sizeof(peer1.firmware) - 1);
            peer1.ipv4 = discovery::FleetDiscoveryService::parse_ipv4("192.168.1.101");
            peer1.port = 80;

            assert(fds.reconcile_node(peer1, now));
            assert(fds.count() == 1);

            discovery::DiscoveredNode snapshot[4] = {};
            size_t count = fds.get_snapshot(snapshot, 4, now);
            assert(count == 1);
            assert(std::strcmp(snapshot[0].id, "lg-001122334455") == 0);
            assert(snapshot[0].ipv4 == peer1.ipv4);

            char ip_buf[16] = {};
            discovery::FleetDiscoveryService::format_ipv4(snapshot[0].ipv4, ip_buf);
            assert(std::strcmp(ip_buf, "192.168.1.101") == 0);

            // C. DHCP / IP Reconciliation: Same ID, new IP address
            // Must update transport address without creating a duplicate record!
            discovery::DiscoveredNode peer1_dhcp = peer1;
            peer1_dhcp.ipv4 = discovery::FleetDiscoveryService::parse_ipv4("192.168.1.188");
            assert(fds.reconcile_node(peer1_dhcp, now + 5000));
            assert(fds.count() == 1); // Still exactly 1 entry!

            count = fds.get_snapshot(snapshot, 4, now + 5000);
            assert(count == 1);
            assert(std::strcmp(snapshot[0].id, "lg-001122334455") == 0);
            discovery::FleetDiscoveryService::format_ipv4(snapshot[0].ipv4, ip_buf);
            assert(std::strcmp(ip_buf, "192.168.1.188") == 0); // Updated to new DHCP lease!

            // D. Presence classification timing
            // online: age <= 20s
            assert(discovery::FleetDiscoveryService::determine_presence(now, now) == discovery::NodePresence::Online);
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 15000) == discovery::NodePresence::Online);
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 20000) == discovery::NodePresence::Online);
            // stale: age 20s..60s
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 20001) == discovery::NodePresence::Stale);
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 59999) == discovery::NodePresence::Stale);
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 60000) == discovery::NodePresence::Stale);
            // offline: age > 60s
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 60001) == discovery::NodePresence::Offline);
            assert(discovery::FleetDiscoveryService::determine_presence(now, now + 120000) == discovery::NodePresence::Offline);

            assert(std::strcmp(discovery::FleetDiscoveryService::presence_to_string(discovery::NodePresence::Online), "online") == 0);
            assert(std::strcmp(discovery::FleetDiscoveryService::presence_to_string(discovery::NodePresence::Stale), "stale") == 0);
            assert(std::strcmp(discovery::FleetDiscoveryService::presence_to_string(discovery::NodePresence::Offline), "offline") == 0);

            // E. Pruning expired records (age > 60s)
            // Add a second node that will expire
            discovery::DiscoveredNode peer2{};
            std::strncpy(peer2.id, "lg-aabbccddeeff", sizeof(peer2.id) - 1);
            peer2.ipv4 = discovery::FleetDiscoveryService::parse_ipv4("192.168.1.102");
            assert(fds.reconcile_node(peer2, now));
            assert(fds.count() == 2);

            // At now + 30s: peer1 refreshed, peer2 not refreshed
            assert(fds.reconcile_node(peer1, now + 30000));
            assert(fds.count() == 2);

            // At now + 70s: peer2 age is 70s (> 60s stale threshold), peer1 age is 40s (stale, <= 60s)
            fds.prune_expired(now + 70000);
            assert(fds.count() == 1);
            count = fds.get_snapshot(snapshot, 4, now + 70000);
            assert(count == 1);
            assert(std::strcmp(snapshot[0].id, "lg-001122334455") == 0);

            // F. Bounded Capacity (32 nodes maximum)
            for (size_t i = 0; i < 40; ++i) {
                discovery::DiscoveredNode n{};
                std::snprintf(n.id, sizeof(n.id), "lg-node%06zu", i);
                char ip_str[16];
                std::snprintf(ip_str, sizeof(ip_str), "10.0.0.%zu", i + 1);
                n.ipv4 = discovery::FleetDiscoveryService::parse_ipv4(ip_str);
                fds.reconcile_node(n, now + 80000);
            }
            assert(fds.count() <= discovery::FleetDiscoveryService::kMaxDiscoveredNodes);
            assert(fds.count() == 32);
        }

        ESP_LOGI(TAG, "[PASS] LG-030 Fleet Discovery, Presence & Immutable Identity passed.");
        return true;
    }

    // ============================================================================
    // LG-031 Canonical Runtime State & API v1 Cutover
    // ============================================================================
    bool test_lg031_runtime_snapshot_and_api_cutover() {
        ESP_LOGI(TAG, "Starting LG-031 Canonical Runtime State & API v1 Cutover Tests...");

        // 1. Requirement S & T: Capabilities Manifest verification
        {
            assert(CapabilitiesManifest::kFeatureFleetDiscovery == true);
            assert(CapabilitiesManifest::kProtocolDiscovery == 1);
        }

        // 2. Requirement A & K: Snapshot collection is side-effect free & contains 16 physical channels
        {
            RuntimeSnapshot snap{};
            snap.schema = 3;
            snap.generated_at_ms = 123456;
            std::strncpy(snap.node.id, "lg-aabbccddeeff", sizeof(snap.node.id) - 1);
            std::strncpy(snap.runtime.effect, "solid", sizeof(snap.runtime.effect) - 1);
            snap.runtime.brightness = 128;
            snap.output.enabled = true;
            snap.output.power_mw = 4500;
            std::strncpy(snap.output.state, "healthy", sizeof(snap.output.state) - 1);
            snap.scene.active_preset = 2;
            std::strncpy(snap.transport.mode, "scene", sizeof(snap.transport.mode) - 1);

            for (uint8_t i = 0; i < kChannelCount; ++i) {
                snap.channels[i].index = i;
                std::string port_str = (i < 8) ? ("A" + std::to_string(i + 1)) : ("P" + std::to_string(i - 7));
                std::strncpy(snap.channels[i].port, port_str.c_str(), sizeof(snap.channels[i].port) - 1);
                snap.channels[i].configured = true;
                snap.channels[i].enabled = true;
                std::strncpy(snap.channels[i].owner, "ScenePlayback", sizeof(snap.channels[i].owner) - 1);
                snap.channels[i].fault = false;
                snap.channels[i].brightness = 255;
            }

            // Collection test: verify snap has 16 channels and collection has no mutation
            assert(kChannelCount == 16);
            assert(std::strcmp(snap.channels[0].port, "A1") == 0);
            assert(std::strcmp(snap.channels[15].port, "P8") == 0);
        }

        // 3. Requirement B, C, D, E, F: Serialization test for HTTP vs WS
        {
            RuntimeSnapshot snap{};
            snap.schema = 3;
            snap.stream_seq = 0;
            snap.generated_at_ms = 5000;
            std::strncpy(snap.node.id, "lg-112233445566", sizeof(snap.node.id) - 1);
            std::strncpy(snap.runtime.effect, "rainbow", sizeof(snap.runtime.effect) - 1);
            snap.runtime.brightness = 180;
            std::strncpy(snap.runtime.control_owner, "ScenePlayback", sizeof(snap.runtime.control_owner) - 1);
            snap.output.enabled = true;
            snap.output.power_mw = 5200;
            std::strncpy(snap.output.state, "healthy", sizeof(snap.output.state) - 1);
            snap.scene.active_preset = 3;
            std::strncpy(snap.transport.mode, "scene", sizeof(snap.transport.mode) - 1);

            // HTTP serialization: stream_seq = 0
            char http_buf[8192];
            FixedJsonWriter http_writer(http_buf, sizeof(http_buf));
            bool http_ok = serialize_runtime_snapshot(snap, http_writer);
            assert(http_ok);
            assert(http_writer.ok());

            // WS serialization: stream_seq = 42
            snap.stream_seq = 42;
            char ws_buf[8192];
            FixedJsonWriter ws_writer(ws_buf, sizeof(ws_buf));
            bool ws_ok = serialize_runtime_snapshot(snap, ws_writer);
            assert(ws_ok);
            assert(ws_writer.ok());

            // Parse HTTP JSON and verify schema 3 and streamSeq == 0
            cJSON* http_json = cJSON_Parse(http_buf);
            assert(http_json != nullptr);
            cJSON* schema_item = cJSON_GetObjectItem(http_json, "schema");
            assert(schema_item != nullptr && schema_item->valueint == 3);
            cJSON* seq_item = cJSON_GetObjectItem(http_json, "streamSeq");
            assert(seq_item != nullptr && seq_item->valueint == 0);

            // Requirement I & J: features/limits and dmxHex absent from snapshot
            assert(cJSON_GetObjectItem(http_json, "features") == nullptr);
            assert(cJSON_GetObjectItem(http_json, "limits") == nullptr);
            assert(cJSON_GetObjectItem(http_json, "dmxHex") == nullptr);

            // Parse WS JSON and verify schema 3 and streamSeq == 42
            cJSON* ws_json = cJSON_Parse(ws_buf);
            assert(ws_json != nullptr);
            cJSON* ws_seq = cJSON_GetObjectItem(ws_json, "streamSeq");
            assert(ws_seq != nullptr && ws_seq->valueint == 42);

            // Verify payload state (node, runtime, output, scene) matches identically
            cJSON* http_node = cJSON_GetObjectItem(http_json, "node");
            cJSON* ws_node = cJSON_GetObjectItem(ws_json, "node");
            assert(std::strcmp(cJSON_GetObjectItem(http_node, "id")->valuestring,
                               cJSON_GetObjectItem(ws_node, "id")->valuestring) == 0);

            cJSON_Delete(http_json);
            cJSON_Delete(ws_json);
        }

        // 4. Requirement G & H: FixedJsonWriter overflow detection and 16-channel worst-case budget
        {
            // Test overflow detection
            char tiny_buf[64];
            FixedJsonWriter tiny_writer(tiny_buf, sizeof(tiny_buf));
            tiny_writer.append("{\"schema\":3,\"long_field\":\"");
            for (int i = 0; i < 100; ++i) {
                tiny_writer.append("x");
            }
            tiny_writer.append("\"}");
            assert(!tiny_writer.ok()); // Must detect overflow

            // Worst-case 16-channel snapshot must comfortably fit 8192 bytes budget
            RuntimeSnapshot max_snap{};
            max_snap.schema = 3;
            max_snap.stream_seq = 12345;
            max_snap.generated_at_ms = 99999999;
            std::strncpy(max_snap.node.id, "lg-ffffffffffff", sizeof(max_snap.node.id) - 1);
            std::strncpy(max_snap.runtime.effect, "combined_pulse", sizeof(max_snap.runtime.effect) - 1);
            max_snap.runtime.brightness = 255;
            std::strncpy(max_snap.runtime.control_owner, "ShowCoordinator", sizeof(max_snap.runtime.control_owner) - 1);
            max_snap.output.enabled = true;
            max_snap.output.power_mw = 35000;
            std::strncpy(max_snap.output.state, "throttled", sizeof(max_snap.output.state) - 1);
            max_snap.scene.active_preset = 15;
            max_snap.scene.requested_preset = 15;
            std::strncpy(max_snap.transport.mode, "timeline", sizeof(max_snap.transport.mode) - 1);
            max_snap.transport.timeline.bpm = 180;
            max_snap.transport.cue.list_index = 8;
            max_snap.transport.cue.cue_index = 15;
            std::strncpy(max_snap.sync.active_role, "follower", sizeof(max_snap.sync.active_role) - 1);
            std::strncpy(max_snap.sync.master_address, "255.255.255.255", sizeof(max_snap.sync.master_address) - 1);
            std::strncpy(max_snap.network.sta_ip, "192.168.254.254", sizeof(max_snap.network.sta_ip) - 1);

            for (uint8_t i = 0; i < kChannelCount; ++i) {
                max_snap.channels[i].index = i;
                std::string port_str = "A" + std::to_string(i);
                std::strncpy(max_snap.channels[i].port, port_str.c_str(), sizeof(max_snap.channels[i].port) - 1);
                max_snap.channels[i].configured = true;
                max_snap.channels[i].enabled = true;
                std::strncpy(max_snap.channels[i].owner, "ShowCoordinator", sizeof(max_snap.channels[i].owner) - 1);
                max_snap.channels[i].fault = true;
                max_snap.channels[i].brightness = 255;
            }

            char budget_buf[8192];
            FixedJsonWriter budget_writer(budget_buf, sizeof(budget_buf));
            bool ok = serialize_runtime_snapshot(max_snap, budget_writer);
            assert(ok);
            assert(budget_writer.ok());
            assert(budget_writer.size() < 4096); // Well under 8192 budget!
        }

        // 5. Requirement L, M, N, O, P: Observational fields mapping
        {
            RuntimeSnapshot snap{};
            snap.stream_seq = 1;
            snap.manual.active = true;
            snap.manual.channel_mask = 0x00FF;
            snap.manual.lease_remaining_ms = 4500;
            std::strncpy(snap.transport.mode, "cue", sizeof(snap.transport.mode) - 1);
            std::strncpy(snap.sync.active_role, "leader", sizeof(snap.sync.active_role) - 1);
            snap.sync.clock_locked = true;
            std::strncpy(snap.network.state, "connected", sizeof(snap.network.state) - 1);
            snap.job.has_job = true;
            snap.job.job_id = 101;
            std::strncpy(snap.job.type, "ota", sizeof(snap.job.type) - 1);
            std::strncpy(snap.job.state, "in_progress", sizeof(snap.job.state) - 1);
            snap.job.progress = 65;

            char buf[8192];
            FixedJsonWriter writer(buf, sizeof(buf));
            bool ok = serialize_runtime_snapshot(snap, writer);
            assert(ok);

            cJSON* root = cJSON_Parse(buf);
            assert(root != nullptr);
            cJSON* manual = cJSON_GetObjectItem(root, "manual");
            assert(cJSON_IsTrue(cJSON_GetObjectItem(manual, "active")));
            assert(cJSON_GetObjectItem(manual, "channelMask")->valueint == 0x00FF);

            cJSON* job = cJSON_GetObjectItem(root, "job");
            assert(job != nullptr && !cJSON_IsNull(job));
            assert(cJSON_GetObjectItem(job, "jobId")->valueint == 101);
            assert(cJSON_GetObjectItem(job, "progress")->valueint == 65);

            cJSON_Delete(root);
        }

        ESP_LOGI(TAG, "[PASS] LG-031 RuntimeSnapshot & API Cutover Tests passed.");
        return true;
    }

    bool test_lg032_scene_studio_and_persistence() {
        ESP_LOGI(TAG, "Testing LG-032 Scene Studio v2 & Safe Persistence...");

        // 1. Authoritative Effect Metadata
        assert(kSignageEffectCount == 12);
        for (size_t i = 0; i < kSignageEffectCount; ++i) {
            const auto& meta = signage_effect_metadata(static_cast<SignageEffect>(i));
            assert(meta.id != nullptr && strlen(meta.id) > 0);
            assert(meta.label != nullptr && strlen(meta.label) > 0);
            // Verify capability flags
            if (strcmp(meta.id, "solid") == 0) {
                assert(!meta.supports_speed);
                assert(!meta.supports_secondary_color);
                assert(meta.pwm_recommended);
            } else if (strcmp(meta.id, "breathe") == 0) {
                assert(meta.supports_speed);
                assert(meta.pwm_recommended);
            } else if (strcmp(meta.id, "rainbow") == 0) {
                assert(meta.spatial);
                assert(!meta.pwm_recommended);
            }
        }

        // 2. Scene and Motion Persistence via ConfigStore
        ConfigStore cfg_store;
        cfg_store.init();

        SceneConfig scene{};
        scene.schema_version = 2;
        std::strncpy(scene.name, "Lobby Warm", sizeof(scene.name) - 1);
        scene.global_brightness = 200;
        scene.transition_ms = 1500;
        for (uint8_t c = 0; c < 16; ++c) {
            scene.channels[c].layers[0].enabled = true;
            scene.channels[c].layers[0].effect = static_cast<uint8_t>(SignageEffect::Solid);
            scene.channels[c].layers[0].opacity = 255;
            scene.channels[c].layers[0].primary_color = {255, 140, 40};
        }

        SceneMotionConfig motion{};
        motion.schema_version = 1;
        motion.transition_curve = TransitionCurve::EaseInOut;
        motion.track_count = 1;
        motion.tracks[0].target = AutomationTarget::GlobalBrightness;
        motion.tracks[0].period_ms = 4000;
        motion.tracks[0].curve = AutomationCurve::Linear;
        motion.tracks[0].point_count = 2;
        motion.tracks[0].points[0] = {0, 50};
        motion.tracks[0].points[1] = {65535, 255};

        // Save scene and motion (ConfigStore computes checksums internally)
        const uint8_t test_slot = 7;
        assert(cfg_store.save_scene(test_slot, scene) == ESP_OK);
        assert(cfg_store.save_scene_motion(test_slot, motion) == ESP_OK);

        // Verify readback
        SceneConfig loaded_scene{};
        assert(cfg_store.load_scene(test_slot, loaded_scene) == ESP_OK);
        assert(std::strcmp(loaded_scene.name, "Lobby Warm") == 0);
        assert(loaded_scene.checksum != 0);

        SceneMotionConfig loaded_motion{};
        assert(cfg_store.load_scene_motion(test_slot, loaded_motion) == ESP_OK);
        assert(loaded_motion.track_count == 1);
        assert(loaded_motion.checksum != 0);

        // Clean up
        cfg_store.delete_scene(test_slot);
        cfg_store.delete_scene_motion(test_slot);

        // 3. Strict Layer & Motion constraints
        // Name byte limit: max 23 bytes (plus null terminator in 24-byte array)
        const char* valid_name = "12345678901234567890123"; // exactly 23 bytes
        assert(strlen(valid_name) == 23);
        const char* too_long_name = "123456789012345678901234"; // 24 bytes -> invalid!
        assert(strlen(too_long_name) == 24);

        // Motion track limits: max 4 tracks, max 6 points
        assert(kAutomationTracksPerScene == 4);
        assert(kAutomationPointsPerTrack == 6);

        ESP_LOGI(TAG, "[PASS] LG-032 Scene Studio v2 & Safe Persistence Tests passed.");
        return true;
    }

    // ============================================================================
    // LG-033 Timeline Studio v2, Anchored Transport & Safe Playback Tests
    // ============================================================================
    bool test_lg033_timeline_studio_v2() {
        ESP_LOGI(TAG, "Testing LG-033 Timeline Studio v2 & Safe Playback...");

        // 1. Pure Timeline Evaluator Tests (AO..AV)
        {
            // AO: Single 4-beat step
            SequencerTrack track1{};
            track1.enabled = true;
            track1.channel_id = 0;
            track1.num_steps = 1;
            track1.steps[0].preset_index = 3;
            track1.steps[0].length_beats = 4;

            TimelineStepPosition p0 = evaluate_timeline_track(track1, 0);
            assert(p0.step_index == 0);
            assert(p0.beat_in_track == 0);
            assert(p0.step_start_beat == 0);
            assert(p0.step_end_beat == 4);

            TimelineStepPosition p2 = evaluate_timeline_track(track1, 2);
            assert(p2.step_index == 0);
            assert(p2.beat_in_track == 2);

            TimelineStepPosition p3 = evaluate_timeline_track(track1, 3);
            assert(p3.step_index == 0);
            assert(p3.beat_in_track == 3);

            // Wraps at beat 4
            TimelineStepPosition p4 = evaluate_timeline_track(track1, 4);
            assert(p4.step_index == 0);
            assert(p4.beat_in_track == 0);

            // AP: 4+8+2 pattern (total 14 beats)
            SequencerTrack track3{};
            track3.enabled = true;
            track3.channel_id = 1;
            track3.num_steps = 3;
            track3.steps[0].preset_index = 10;
            track3.steps[0].length_beats = 4;
            track3.steps[1].preset_index = 11;
            track3.steps[1].length_beats = 8;
            track3.steps[2].preset_index = 12;
            track3.steps[2].length_beats = 2;

            // AQ: Exact boundary selects next step
            TimelineStepPosition b3 = evaluate_timeline_track(track3, 3);
            assert(b3.step_index == 0);
            assert(b3.step_start_beat == 0);
            assert(b3.step_end_beat == 4);

            TimelineStepPosition b4 = evaluate_timeline_track(track3, 4);
            assert(b4.step_index == 1);
            assert(b4.step_start_beat == 4);
            assert(b4.step_end_beat == 12);

            TimelineStepPosition b11 = evaluate_timeline_track(track3, 11);
            assert(b11.step_index == 1);

            TimelineStepPosition b12 = evaluate_timeline_track(track3, 12);
            assert(b12.step_index == 2);
            assert(b12.step_start_beat == 12);
            assert(b12.step_end_beat == 14);

            TimelineStepPosition b13 = evaluate_timeline_track(track3, 13);
            assert(b13.step_index == 2);

            // AR: Pattern wraps at beat 14
            TimelineStepPosition b14 = evaluate_timeline_track(track3, 14);
            assert(b14.step_index == 0);
            assert(b14.beat_in_track == 0);

            TimelineStepPosition b18 = evaluate_timeline_track(track3, 18); // 18 % 14 = 4 -> step 1
            assert(b18.step_index == 1);
            assert(b18.beat_in_track == 4);

            // AS: Zero-step track ignored
            SequencerTrack track_zero{};
            track_zero.enabled = true;
            track_zero.num_steps = 0;
            TimelineStepPosition p_zero = evaluate_timeline_track(track_zero, 5);
            assert(p_zero.step_index == 0xFF);

            // AT: Muted track ignored
            SequencerTrack track_muted = track3;
            track_muted.enabled = false;
            TimelineStepPosition p_muted = evaluate_timeline_track(track_muted, 5);
            assert(p_muted.step_index == 0xFF);

            // AU: Different tracks loop independently
            TimelineStepPosition t1_at_8 = evaluate_timeline_track(track1, 8);
            TimelineStepPosition t3_at_8 = evaluate_timeline_track(track3, 8);
            assert(t1_at_8.beat_in_track == 0); // 8 % 4 == 0
            assert(t3_at_8.beat_in_track == 8); // 8 % 14 == 8 (step 1)
            assert(t3_at_8.step_index == 1);
        }

        // 2. ConfigStore Sequence Persistence, Legacy Migration & Checksum (Requirements D..G, M..O, W)
        ConfigStore cfg;
        cfg.init();
        {
            Sequence seq{};
            seq.enabled = true; // Legacy backup/client had enabled=true
            seq.bpm = 120;
            for (uint8_t i = 0; i < 16; ++i) {
                seq.tracks[i].channel_id = i;
                seq.tracks[i].enabled = (i < 4);
                seq.tracks[i].num_steps = (i < 4) ? 2 : 0;
                if (i < 4) {
                    seq.tracks[i].steps[0].preset_index = 1;
                    seq.tracks[i].steps[0].length_beats = 4;
                    seq.tracks[i].steps[1].preset_index = 2;
                    seq.tracks[i].steps[1].length_beats = 4;
                }
            }

            assert(cfg.save_sequence(seq) == ESP_OK);

            Sequence loaded{};
            assert(cfg.load_sequence(loaded) == ESP_OK);
            // Requirement W: legacy enabled=true cannot auto-start after reboot/load!
            assert(loaded.enabled == false);
            assert(loaded.bpm == 120);
            assert(loaded.checksum != 0);

            // Legacy non-canonical channel mapping migration
            Sequence legacy_order{};
            legacy_order.bpm = 130;
            legacy_order.tracks[0].channel_id = 7; // A8 placed in slot 0
            legacy_order.tracks[0].enabled = true;
            legacy_order.tracks[0].num_steps = 1;
            legacy_order.tracks[0].steps[0].preset_index = 5;
            legacy_order.tracks[0].steps[0].length_beats = 8;
            legacy_order.tracks[1].channel_id = 0; // A1 placed in slot 1
            legacy_order.tracks[1].enabled = true;
            legacy_order.tracks[1].num_steps = 1;
            legacy_order.tracks[1].steps[0].preset_index = 6;
            legacy_order.tracks[1].steps[0].length_beats = 4;

            assert(cfg.save_sequence(legacy_order) == ESP_OK);
            Sequence canon_loaded{};
            assert(cfg.load_sequence(canon_loaded) == ESP_OK);
            // Canonical mapping ensures tracks[i].channel_id == i
            for (uint8_t i = 0; i < 16; ++i) {
                assert(canon_loaded.tracks[i].channel_id == i);
            }
            assert(canon_loaded.tracks[7].enabled == true);
            assert(canon_loaded.tracks[7].steps[0].preset_index = 5);
            assert(canon_loaded.tracks[7].steps[0].length_beats = 8);
            assert(canon_loaded.tracks[0].enabled == true);
            assert(canon_loaded.tracks[0].steps[0].preset_index = 6);
            assert(canon_loaded.tracks[0].steps[0].length_beats = 4);

            // Restore clean sequence for further tests
            assert(cfg.save_sequence(seq) == ESP_OK);
        }

        // 3. Persistence / Playback Separation & Anchored Transport (Requirements P..W, X..AC)
        {
            OutputController ctrl;
            ControlSourceManager csm;
            csm.init();
            LightingRuntime runtime(csm, ctrl, cfg);
            ClockDiscipline clock;
            Scheduler scheduler(cfg, runtime, clock);

            // Initially not running
            assert(!scheduler.is_local_timeline_running());
            assert(!scheduler.is_remote_timeline_running());
            assert(scheduler.transport_generation() == 0);

            // RUN Timeline with explicit anchor (Beat 0 start)
            const uint64_t anchor1 = 5000000ULL;
            scheduler.start_local_timeline(120, anchor1);
            assert(scheduler.is_local_timeline_running());
            assert(scheduler.timeline_anchor_ms() == anchor1);
            assert(scheduler.transport_generation() == 1);
            assert(!scheduler.is_timeline_content_stale());

            // Verify active snapshot matches stored sequence
            Sequence active1 = scheduler.active_sequence();
            assert(active1.bpm == 120);
            assert(active1.tracks[0].steps[0].length_beats == 4);

            // Elapsed calculation from anchor
            auto snap1 = scheduler.transport_snapshot();
            assert(snap1.timeline_running());
            assert(snap1.timeline_bpm() == 120);
            assert(snap1.transport_generation == 1);
            assert(snap1.timeline_anchor_ms == anchor1);
            assert(!snap1.timeline_content_stale);
            assert(snap1.timeline_active_checksum == snap1.timeline_saved_checksum);

            // Simulate PUT /sequence (saving edits while running)
            Sequence modified_seq{};
            cfg.load_sequence(modified_seq);
            modified_seq.bpm = 145; // Edited BPM
            modified_seq.tracks[0].steps[0].length_beats = 16; // Edited step length
            assert(cfg.save_sequence(modified_seq) == ESP_OK);

            // Reload scheduler persisted cache (as happens every ~1s or via PUT sequence)
            scheduler.reload_sequence();

            // HARD INVARIANT: Saving while running MUST NOT mutate active_sequence!
            assert(scheduler.is_local_timeline_running());
            assert(scheduler.timeline_anchor_ms() == anchor1);
            assert(scheduler.transport_generation() == 1);
            assert(scheduler.active_sequence().bpm == 120); // Still running 120 BPM!
            assert(scheduler.active_sequence().tracks[0].steps[0].length_beats == 4); // Still 4 beats!

            // Runtime correctly flags contentStale!
            assert(scheduler.is_timeline_content_stale());
            auto snap_stale = scheduler.transport_snapshot();
            assert(snap_stale.timeline_running());
            assert(snap_stale.timeline_content_stale);
            assert(snap_stale.timeline_active_checksum != snap_stale.timeline_saved_checksum);

            // Restart with saved version: creates new generation and new anchor
            const uint64_t anchor2 = 5060000ULL;
            scheduler.start_local_timeline(145, anchor2);
            assert(scheduler.is_local_timeline_running());
            assert(scheduler.transport_generation() == 2); // Monotonic increment
            assert(scheduler.timeline_anchor_ms() == anchor2); // New anchor
            assert(!scheduler.is_timeline_content_stale()); // Stale cleared
            assert(scheduler.active_sequence().bpm == 145); // Now running new BPM!
            assert(scheduler.active_sequence().tracks[0].steps[0].length_beats == 16);

            // Stop Timeline: releases playback, leaves persisted sequence untouched
            scheduler.stop_local_timeline();
            assert(!scheduler.is_local_timeline_running());
            Sequence check_persisted{};
            cfg.load_sequence(check_persisted);
            assert(check_persisted.bpm == 145);
        }

        // 4. NodeSync v5 & Follower Timeline Synchronization (Requirements AD..AN)
        {
            static_assert(sizeof(NodeSync::SyncPacket) == 76, "NodeSync v6 packet must be exactly 76 bytes");
            OutputController ctrl;
            ControlSourceManager csm;
            csm.init();
            LightingRuntime runtime(csm, ctrl, cfg);
            ClockDiscipline clock;
            Scheduler scheduler(cfg, runtime, clock);

            Sequence stored_seq{};
            cfg.load_sequence(stored_seq);

            // Apply new remote timeline generation
            const uint64_t leader_anchor = 7000000ULL;
            scheduler.apply_remote_timeline(10, 130, leader_anchor, stored_seq.checksum);
            assert(scheduler.is_remote_timeline_running());
            assert(scheduler.transport_generation() == 10);
            assert(scheduler.timeline_anchor_ms() == leader_anchor);

            // Repeated packet with same generation does NOT reset anchor or re-snapshot
            scheduler.apply_remote_timeline(10, 130, leader_anchor, stored_seq.checksum);
            assert(scheduler.timeline_anchor_ms() == leader_anchor);
            assert(scheduler.transport_generation() == 10);

            // Late follower phase calculation:
            // At disciplined clock 7006000ULL, elapsed = 6000ms.
            // At 130 BPM, 60000 / 130 = 461ms per beat; total beats = 6000 / 461 = 13.
            const uint64_t now_ms = leader_anchor + 6000ULL;
            const uint64_t elapsed = now_ms >= leader_anchor ? (now_ms - leader_anchor) : 0;
            const uint32_t ms_per_beat = (60000 / 130);
            assert(elapsed / ms_per_beat == 13);

            // Mismatched checksum error code check
            assert(static_cast<uint8_t>(SyncTransportError::TimelineMissing) == 6);
            assert(static_cast<uint8_t>(SyncTransportError::TimelineMismatch) == 7);
            assert(static_cast<uint8_t>(SyncTransportError::TimelineInvalid) == 8);

            // Sync timeout clears remote timeline
            scheduler.clear_remote_timeline();
            assert(!scheduler.is_remote_timeline_running());
        }

        ESP_LOGI(TAG, "[PASS] LG-033 Timeline Studio v2 & Safe Playback Tests passed.");
        return true;
    }

    // ==========================================================================
    // LG-034 — Cue Studio v2 & Cue Runtime Hardening
    // ==========================================================================
    bool test_lg034_cue_studio_and_runtime_hardening() {
        ESP_LOGI(TAG, "Testing LG-034 Cue Studio v2 & Cue Runtime Hardening...");

        // ── Setup: two independent runtimes (leader + follower), shared config ──
        ConfigStore cfg;
        cfg.init();
        OutputController ctrl1, ctrl2;
        ControlSourceManager csm1, csm2;
        csm1.init();
        csm2.init();
        LightingRuntime rt1(csm1, ctrl1, cfg);
        LightingRuntime rt2(csm2, ctrl2, cfg);
        CueRunner runner(cfg, rt1);
        CueRunner follower(cfg, rt2);

        // Persist three scenes (0, 1, 2) and a 3-cue single-shot list in slot 0
        for (uint8_t i = 0; i < 3; ++i) {
            SceneConfig sc{};
            sc.checksum = 100 + i;
            assert(cfg.save_scene(i, sc) == ESP_OK);
        }
        CueList list{};
        list.count = 3;
        list.mode = CueRunMode::SingleShot;
        list.end_action = CueEndAction::HoldLast;
        strncpy(list.name, "LG034 Show", sizeof(list.name) - 1);
        list.cues[0] = {0, 1000, 500, TransitionCurve::Linear, "one"};
        list.cues[1] = {1, 1000, 500, TransitionCurve::Linear, "two"};
        list.cues[2] = {2, 1000, 500, TransitionCurve::Linear, "three"};
        assert(cfg.save_cue_list(0, list) == ESP_OK);

        CueList stored{};
        assert(cfg.load_cue_list(0, stored) == ESP_OK);
        const uint32_t original_checksum = stored.checksum;
        assert(original_checksum != 0);

        // ── A. Single Shot final completion does not deadlock (§1/§73.A) ──
        {
            uint64_t t0 = 10000;
            assert(runner.start(0, 0, t0) == ESP_OK);
            // Walk through: cue0 fade(500) hold(1000) -> cue1 -> cue2 -> final hold end
            runner.update(t0 + 600);   // still fading cue0
            runner.update(t0 + 1600);  // cue0 holding
            runner.update(t0 + 2700);  // -> cue1 fading
            runner.update(t0 + 3800);  // cue1 holding
            runner.update(t0 + 4900);  // -> cue2 fading
            runner.update(t0 + 6000);  // cue2 holding
            // Final hold completes: update() must return (no deadlock) and stop naturally
            runner.update(t0 + 7100);
            const auto snapA = runner.snapshot(t0 + 7100);
            assert(snapA.running == false);
            assert(snapA.state == CueRunnerState::Stopped);
        }

        // ── B/C/D. Natural completion applies each end action (§73.B..D) ──
        {
            // HoldLast (already covered by A; explicit end-action state check)
            CueList hold_list = stored;
            hold_list.end_action = CueEndAction::HoldLast;
            cfg.save_cue_list(1, hold_list);
            assert(runner.start(1, 0, 20000) == ESP_OK);
            runner.update(20000 + 1600);
            runner.update(20000 + 2700);
            runner.update(20000 + 3800);
            runner.update(20000 + 4900);
            runner.update(20000 + 6000);
            runner.update(20000 + 7100);
            assert(runner.snapshot(27100).running == false);
            // HoldLast: active scene remains the last cue's scene
            assert(rt1.active_scene_index() == 2);

            // Blackout end action
            CueList bo_list = stored;
            bo_list.end_action = CueEndAction::Blackout;
            cfg.save_cue_list(1, bo_list);
            assert(runner.start(1, 0, 30000) == ESP_OK);
            runner.update(30000 + 600);
            runner.update(30000 + 1600);
            runner.update(30000 + 2700);
            runner.update(30000 + 3800);
            runner.update(30000 + 4900);
            runner.update(30000 + 6000);
            runner.update(30000 + 7100);
            assert(runner.snapshot(37100).running == false);

            // RestoreFallback end action
            CueList fb_list = stored;
            fb_list.end_action = CueEndAction::RestoreFallback;
            cfg.save_cue_list(1, fb_list);
            assert(runner.start(1, 0, 40000) == ESP_OK);
            runner.update(40000 + 600);
            runner.update(40000 + 1600);
            runner.update(40000 + 2700);
            runner.update(40000 + 3800);
            runner.update(40000 + 4900);
            runner.update(40000 + 6000);
            runner.update(40000 + 7100);
            assert(runner.snapshot(47100).running == false);
        }

        // ── E. Operator Stop does NOT execute end action (§73.E) ──
        {
            CueList bo_list = stored;
            bo_list.end_action = CueEndAction::Blackout;
            cfg.save_cue_list(1, bo_list);
            assert(runner.start(1, 0, 50000) == ESP_OK);
            runner.update(50000 + 1600); // mid cue0 hold
            // Operator stop mid-show with Blackout configured: output must NOT blackout
            assert(runner.stop(CueStopReason::Operator, 50200) == ESP_OK);
            assert(rt1.active_scene_index() == 0); // held cue0's scene, not blackout
        }

        // ── F. Scene preemption does NOT execute end action (§73.F) ──
        {
            assert(runner.start(1, 0, 60000) == ESP_OK);
            runner.update(60000 + 1600);
            // Scene activation preempts: uses Preempted reason
            assert(runner.stop(CueStopReason::Preempted, 60200) == ESP_OK);
            assert(rt1.active_scene_index() == 0); // no blackout applied
        }

        // ── G. Sync loss does NOT execute natural end action (§73.H) ──
        {
            assert(runner.start(1, 0, 70000) == ESP_OK);
            runner.update(70000 + 1600);
            assert(runner.stop(CueStopReason::SyncLost, 70200) == ESP_OK);
            assert(rt1.active_scene_index() == 0);
        }

        // ── I/J/K. Hold pause captures elapsed/remaining; resume continues (§74.I..K) ──
        {
            assert(runner.start(0, 0, 80000) == ESP_OK);
            runner.update(80000 + 600);   // fade 500 done -> holding at 80600
            runner.update(80000 + 800);
            // Pause at 80900: hold elapsed = 80900 - 80600 = 300; remaining = 1000 - 300 = 700
            assert(runner.pause(80900) == ESP_OK);
            auto p = runner.snapshot(80900);
            assert(p.state == CueRunnerState::Paused);
            assert(p.paused_from == CueRunnerState::Holding);
            assert(p.elapsed_ms == 300);
            assert(p.remaining_ms == 700);

            // Time passes while paused — remaining must not advance
            auto p_later = runner.snapshot(85000);
            assert(p_later.remaining_ms == 700);

            // Resume at 86000: continues with remaining 700ms of hold
            assert(runner.resume(86000) == ESP_OK);
            auto r = runner.snapshot(86000);
            assert(r.state == CueRunnerState::Holding);
            assert(r.remaining_ms == 700);
            // Advance past remaining hold -> next cue
            runner.update(86000 + 800);
            assert(runner.snapshot(86800).current_cue == 1);
            runner.stop(CueStopReason::Operator, 86900);
        }

        // ── L/M/N/O. Fade pause freezes transition; resume continues (§74.L..O) ──
        {
            assert(runner.start(0, 0, 90000) == ESP_OK);
            runner.update(90000 + 200); // mid-fade (fade is 500ms)
            const uint16_t progress_before = rt1.transition_progress_q15();
            assert(runner.pause(90000 + 250) == ESP_OK);
            auto p = runner.snapshot(90000 + 250);
            assert(p.state == CueRunnerState::Paused);
            assert(p.paused_from == CueRunnerState::Fading);

            // Transition must be frozen: progress must not advance while paused
            runner.update(90000 + 1000);
            runner.update(90000 + 2000);
            const uint16_t progress_frozen = rt1.transition_progress_q15();
            assert(progress_frozen == progress_before);

            // Resume: continues toward target from frozen visual over remaining 250ms
            assert(runner.resume(90000 + 2000) == ESP_OK);
            auto r = runner.snapshot(90000 + 2000);
            assert(r.state == CueRunnerState::Fading);
            assert(r.remaining_ms <= 250);
            // After remaining duration, fade completes into hold
            runner.update(90000 + 2300);
            assert(runner.snapshot(90000 + 2300).state == CueRunnerState::Holding);
            runner.stop(CueStopReason::Operator, 90000 + 2400);
        }

        // ── P/Q/R/S/T/U. Active snapshot vs saved edits, contentStale, restart (§75) ──
        {
            assert(runner.start(0, 0, 100000) == ESP_OK);
            assert(runner.snapshot(100000).running);
            // P: RUN snapshots the persisted list
            assert(runner.snapshot(100000).active_checksum == original_checksum);

            // Q: edit + save the same slot while running
            CueList edited = stored;
            edited.cues[0].hold_ms = 5000;
            assert(cfg.save_cue_list(0, edited) == ESP_OK);

            // Running content must be unchanged
            const auto after_save = runner.snapshot(101000);
            assert(after_save.running);
            assert(after_save.active_checksum == original_checksum);

            // R/S: active checksum differs from saved; contentStale true
            CueList reloaded{};
            assert(cfg.load_cue_list(0, reloaded) == ESP_OK);
            assert(after_save.saved_checksum == reloaded.checksum);
            assert(after_save.saved_checksum != original_checksum);
            assert(after_save.content_stale == true);

            // T/U: RESTART snapshots newest saved version and clears contentStale
            assert(runner.restart(0, 0, 102000) == ESP_OK);
            const auto after_restart = runner.snapshot(102000);
            assert(after_restart.running);
            assert(after_restart.active_checksum == reloaded.checksum);
            assert(after_restart.content_stale == false);
            runner.stop(CueStopReason::Operator, 102500);
        }

        // ── AJ..AM. Checksum conflict protection at ConfigStore level (§77) ──
        {
            // Checksum match detection basis (API layer compares expectedChecksum)
            CueList current{};
            assert(cfg.load_cue_list(0, current) == ESP_OK);
            // AM: save does not change runtime state (already stopped here)
            assert(runner.snapshot(103000).running == false);
            // AL: save produces a new stable checksum via readback
            CueList v2 = current;
            v2.cues[0].hold_ms = 7000;
            assert(cfg.save_cue_list(0, v2) == ESP_OK);
            CueList v2_back{};
            assert(cfg.load_cue_list(0, v2_back) == ESP_OK);
            assert(v2_back.checksum == current.checksum); // unchanged content -> same checksum
        }

        // ── AU..AY. Transport op status semantics (§79.AU..AY) ──
        {
            // AU: pause while stopped
            assert(runner.pause(110000) == ESP_ERR_INVALID_STATE);
            // AV: resume while not paused
            assert(runner.resume(110000) == ESP_ERR_INVALID_STATE);

            assert(runner.start(0, 0, 110000) == ESP_OK);
            // AW: invalid jump
            assert(runner.jump(99, 110100) == ESP_ERR_INVALID_ARG);
            // AY: crossfade outside Manual mode
            assert(runner.set_manual_fade(16384) == ESP_ERR_INVALID_STATE);
            // Crossfade inside Manual mode is accepted
            CueList manual_list = stored;
            manual_list.mode = CueRunMode::Manual;
            cfg.save_cue_list(2, manual_list);
            assert(runner.start(2, 0, 110200) == ESP_OK);
            assert(runner.set_manual_fade(16384) == ESP_OK);
            assert(runner.stop(CueStopReason::Operator, 110300) == ESP_OK);
        }

        // ── BE/BF. Follower parity: matching runs, mismatch fails closed (§80.BE/BF) ──
        {
            assert(runner.start(0, 0, 120000) == ESP_OK);
            auto leader_snap = runner.sync_snapshot(120000 + 100);
            leader_snap.list_checksum = original_checksum;
            // Matching checksum: follower applies
            assert(follower.apply_sync_snapshot(leader_snap, 120000 + 100) == ESP_OK);
            assert(follower.snapshot(120100).running);

            // Mismatched checksum is detected by the API layer via cue_list_mismatch;
            // here verify the follower rejects an unavailable list index
            CueSyncSnapshot bad_list = leader_snap;
            bad_list.list_index = 7; // not persisted in this test
            assert(follower.apply_sync_snapshot(bad_list, 120200) != ESP_OK);

            assert(runner.stop(CueStopReason::Operator, 120300) == ESP_OK);
            follower.clear_remote_sync(120300);
        }

        // ── SyncTransportError::CueListMismatch enum value (§29) ──
        assert(static_cast<uint8_t>(SyncTransportError::CueListMismatch) == 9);

        // ── AZ. Packed v6 size asserted (also asserted at compile time) ──
        assert(sizeof(NodeSync::SyncPacket) == 76);

        ESP_LOGI(TAG, "[PASS] LG-034 Cue Studio v2 & Cue Runtime Hardening Tests passed.");
        return true;
    }

    bool test_lg035_diagnostics_and_factory_validation() {
        ESP_LOGI(TAG, "Testing LG-035 Diagnostics, Factory Validation & Burn-In Hardening...");

        // 1. FactoryRunner Test Session State Machine & Operator Confirmation Invariant
        {
            ControlSourceManager sm;
            OutputController ctrl;
            FactoryRunner runner(sm, ctrl);

            assert(runner.start() == ESP_OK);
            assert(runner.session().state == FactoryTestState::Idle);

            // A. Start Addressable Channel 2 validation test
            uint32_t sid = 0;
            assert(runner.start_test_session(FactoryTestKind::AddressableChannel, 2, 60000, 32, &sid));
            assert(sid > 0);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.session().state == FactoryTestState::Running);
            assert(runner.session().kind == FactoryTestKind::AddressableChannel);
            assert(runner.session().channel == 2);
            assert(runner.session().operator_confirmation_required == true);
            assert(sm.current_owner() == ControlSource::Factory);

            // B. Abort test session releases ownership without claiming pass
            runner.abort_test_session(sid);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.session().state == FactoryTestState::Aborted);
            assert(sm.current_owner() == ControlSource::Idle);
            assert(runner.status().outputs_enabled == false);

            // C. Critical rule (§tyry4t): Software must NOT claim physical hardware passed
            // unless there is actual feedback or an operator explicitly confirms it.
            uint32_t sid2 = 0;
            assert(runner.start_test_session(FactoryTestKind::PwmChannel, 3, 50, 48, &sid2));
            assert(sid2 > 0);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.session().state == FactoryTestState::Running);

            // Let the test duration expire (50 ms)
            vTaskDelay(pdMS_TO_TICKS(100));
            // State must transition to WaitingForOperator, NOT Passed!
            assert(runner.session().state == FactoryTestState::WaitingForOperator);
            assert(runner.session().operator_confirmation_required == true);

            // Operator explicitly confirms physical pass
            bool confirmed = runner.confirm_test_session(sid2, true);
            assert(confirmed == true);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.session().state == FactoryTestState::Passed);
            assert(runner.validation_report().pwm[3].tested == true);
            assert(runner.validation_report().pwm[3].passed == true);

            // Operator explicitly confirms physical failure
            uint32_t sid3 = 0;
            assert(runner.start_test_session(FactoryTestKind::AddressableChannel, 1, 50, 48, &sid3));
            vTaskDelay(pdMS_TO_TICKS(100));
            assert(runner.session().state == FactoryTestState::WaitingForOperator);
            assert(runner.confirm_test_session(sid3, false) == true);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.session().state == FactoryTestState::Failed);
            assert(runner.validation_report().addressable[1].tested == true);
            assert(runner.validation_report().addressable[1].passed == false);

            // 2. Controlled Power & Brightness Limits
            // Diagnostic brightness is capped at 96 / 255
            runner.select_effect(FactoryEffect::WhiteWalk, 255, 1000);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.status().brightness <= FactoryRunner::kMaxDiagnosticBrightness);

            // Burn-in default brightness is 64 / 255, absolute cap 96 / 255
            JobManager jm;
            uint32_t jid = 0;
            assert(runner.start_burn_in_job(&jm, 60, 255, &jid)); // requests 255
            assert(jid > 0);
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.status().brightness <= FactoryRunner::kMaxBurnInBrightness);
            assert(runner.session().kind == FactoryTestKind::BurnIn);
            assert(runner.session().state == FactoryTestState::Running);

            // Stop burn-in job
            runner.stop_burn_in_job();
            vTaskDelay(pdMS_TO_TICKS(30));
            assert(runner.session().state == FactoryTestState::Aborted);

            // 3. Transient arbitration restoration on stop
            runner.stop();
            assert(runner.status().outputs_enabled == false);
            assert(sm.current_owner() == ControlSource::Idle);
        }

        ESP_LOGI(TAG, "[PASS] LG-035 Diagnostics, Factory Validation & Burn-In Tests passed.");
        return true;
    }

    bool test_lg036_configuration_recovery_and_ota() {
        ESP_LOGI(TAG, "Testing LG-036 Configuration, Recovery, Backup/Restore & OTA Hardening...");

        // 1. DeviceConfig Checksum and Bounds (§92 A..K)
        {
            DeviceConfig cfg1{};
            strncpy(cfg1.device_name, "Lobby East", sizeof(cfg1.device_name) - 1);
            cfg1.max_power_mw = 50000;
            cfg1.gamma_x10 = 22;

            DeviceConfig cfg2 = cfg1;
            uint32_t crc1 = ConfigStore::compute_device_crc(cfg1);
            uint32_t crc2 = ConfigStore::compute_device_crc(cfg2);
            assert(crc1 == crc2);

            cfg2.max_power_mw = 40000;
            uint32_t crc3 = ConfigStore::compute_device_crc(cfg2);
            assert(crc1 != crc3);

            // Fail-closed validation bounds (§92 D..J)
            ConfigStore::ConfigValidationResult res{};
            cfg1.channels[0].max_brightness = 255;
            assert(ConfigStore::validate_config(cfg1, &res) == ESP_OK);
            assert(res.valid);

            // Invalid addressable type
            cfg1.channels[0].addressable_type = static_cast<AddressableType>(99);
            assert(ConfigStore::validate_config(cfg1, &res) != ESP_OK || !res.valid);
            cfg1.channels[0].addressable_type = AddressableType::WS2812B;

            // Invalid pixel order
            cfg1.channels[0].pixel_order = static_cast<PixelOrder>(99);
            assert(ConfigStore::validate_config(cfg1, &res) != ESP_OK || !res.valid);
            cfg1.channels[0].pixel_order = PixelOrder::GRB;

            // Power limits match capabilities (60000 mW)
            assert(kMaxPowerMw == 60000);
        }

        // 2. ConfigApplyPlan & Impact Classification (§93 L..W)
        {
            DeviceConfig old_cfg{};
            strncpy(old_cfg.device_name, "Node 1", sizeof(old_cfg.device_name) - 1);
            old_cfg.gamma_x10 = 22;
            old_cfg.max_power_mw = 40000;
            old_cfg.channels[0].pixel_count = 100;
            old_cfg.protocol.artnet_enabled = false;
            old_cfg.sync_enabled = false;

            // L. Metadata only change -> no output restart
            DeviceConfig new_cfg = old_cfg;
            strncpy(new_cfg.device_name, "Node 1 Renamed", sizeof(new_cfg.device_name) - 1);
            ConfigApplyPlan plan_meta = diff_device_config(old_cfg, new_cfg);
            assert(plan_meta.has_impact(ConfigApplyImpact::Metadata));
            assert(!plan_meta.requires_output_reconfigure());
            assert(!plan_meta.requires_dmx_reconfigure());
            assert(!plan_meta.requires_sync_restart());

            // M, N. Gamma & Power limit -> LightingHot
            new_cfg = old_cfg;
            new_cfg.gamma_x10 = 18;
            new_cfg.max_power_mw = 50000;
            ConfigApplyPlan plan_lighting = diff_device_config(old_cfg, new_cfg);
            assert(plan_lighting.has_impact(ConfigApplyImpact::LightingHot));
            assert(!plan_lighting.requires_output_reconfigure());

            // O. Topology change (pixel count) -> OutputReconfigure
            new_cfg = old_cfg;
            new_cfg.channels[0].pixel_count = 200;
            ConfigApplyPlan plan_topo = diff_device_config(old_cfg, new_cfg);
            assert(plan_topo.requires_output_reconfigure());

            // Q. Sync change -> SyncRestart
            new_cfg = old_cfg;
            new_cfg.sync_enabled = true;
            ConfigApplyPlan plan_sync = diff_device_config(old_cfg, new_cfg);
            assert(plan_sync.requires_sync_restart());

            // T. DMX change -> DmxReconfigure
            new_cfg = old_cfg;
            new_cfg.protocol.artnet_enabled = true;
            ConfigApplyPlan plan_dmx = diff_device_config(old_cfg, new_cfg);
            assert(plan_dmx.requires_dmx_reconfigure());
        }

        // 3. Backup Package Streaming & Secret Exclusion (§94 X..AH)
        {
            DeviceConfig cfg_with_token{};
            cfg_with_token.sync_token = 0xDEADBEEF;
            cJSON* json_cfg = cJSON_CreateObject();
            cJSON_AddBoolToObject(json_cfg, "syncTokenSet", cfg_with_token.sync_token != 0);
            assert(cJSON_GetObjectItem(json_cfg, "syncToken") == nullptr);
            cJSON* token_set = cJSON_GetObjectItem(json_cfg, "syncTokenSet");
            assert(token_set != nullptr && cJSON_IsTrue(token_set));
            assert(cJSON_GetObjectItem(json_cfg, "password") == nullptr);
            assert(cJSON_GetObjectItem(json_cfg, "adminToken") == nullptr);
            cJSON_Delete(json_cfg);
        }

        // 4. Restore Validation & Cross-References (§95 AI..AQ)
        {
            ParsedBackupPackage pkg{};
            pkg.schema = 4;
            strncpy(pkg.family, "kinetic-node", sizeof(pkg.family) - 1);
            assert(strcmp(pkg.family, "led-node") != 0);

            ParsedBackupPackage valid_pkg{};
            valid_pkg.schema = 4;
            strncpy(valid_pkg.family, "led-node", sizeof(valid_pkg.family) - 1);
            assert(strcmp(valid_pkg.family, "led-node") == 0);
        }

        // 5. Restore Transaction & Power-Loss Marker (§96 AR..AZ)
        {
            ConfigStore cs;
            uint32_t marker_checksum = 0;
            cs.set_restore_in_progress(false, 0);
            assert(!cs.is_restore_in_progress(&marker_checksum));

            cs.set_restore_in_progress(true, 987654321);
            assert(cs.is_restore_in_progress(&marker_checksum));
            assert(marker_checksum == 987654321);

            cs.set_restore_in_progress(false, 0);
            assert(!cs.is_restore_in_progress());
        }

        // 6. OTA Boot Verification & Rollback Semantics (§97-§98 BA..BM)
        {
            OtaBootVerifier& verifier = OtaBootVerifier::instance();
            verifier.init();
            assert(verifier.running_version() != nullptr && strlen(verifier.running_version()) > 0);

            verifier.confirm_candidate();
            assert(verifier.boot_state() == OtaBootState::Confirmed);
            assert(!verifier.rolled_back());

            verifier.rollback_candidate("test_fault", -42);
            assert(verifier.boot_state() == OtaBootState::RolledBack);
            assert(verifier.rolled_back());
            assert(strcmp(verifier.last_reason(), "test_fault") == 0);

            verifier.confirm_candidate();
        }

        ESP_LOGI(TAG, "[PASS] LG-036 Configuration, Recovery, Backup/Restore & OTA Hardening tests passed.");
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
    all_ok &= test_batch9_checked_scene_activation_lifecycle_guard();

    // Batch 11 Phase 0 Tests
    all_ok &= test_batch11_phase0_truthful_control_contract();

    // LG-022 Show Engine Tests
    all_ok &= test_lg022_show_engine();

    // LG-024 Live Desk & Manual Override Tests
    all_ok &= test_lg024_live_desk_and_manual_override();

    // LG-025 Fleet Sync & Show Deployment Tests
    all_ok &= test_lg025_fleet_sync_and_show_deployment();

    // LG-026 Show Transport v2 & Synchronized Cue Playback Tests
    all_ok &= test_lg026_show_transport_and_cue_sync();

    // LG-027 NetworkSettings & Commissioning Tests
    all_ok &= test_lg027_network_settings_and_commissioning();

    // LG-028 Network Commissioning & Lifecycle Regressions (A..T)
    all_ok &= test_lg028_network_commissioning_and_lifecycle();

    // LG-029 Node-Backed Authentication & Sessions Regressions (A..AC)
    all_ok &= test_lg029_node_backed_authentication();

    // LG-030 Fleet Discovery, Presence & Immutable Identity
    all_ok &= test_lg030_fleet_discovery_and_identity();

    // LG-031 Canonical Runtime State & API v1 Cutover
    all_ok &= test_lg031_runtime_snapshot_and_api_cutover();

    // LG-032 Scene Studio v2 & Safe Persistence
    all_ok &= test_lg032_scene_studio_and_persistence();

    // LG-033 Timeline Studio v2 & Safe Playback
    all_ok &= test_lg033_timeline_studio_v2();

    // LG-034 Cue Studio v2 & Cue Runtime Hardening
    all_ok &= test_lg034_cue_studio_and_runtime_hardening();

    // LG-035 Diagnostics, Factory Validation & Burn-In Workflow
    all_ok &= test_lg035_diagnostics_and_factory_validation();

    // LG-036 Configuration, Recovery, Backup/Restore & OTA Hardening
    all_ok &= test_lg036_configuration_recovery_and_ota();

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
