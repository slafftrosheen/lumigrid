// Reuse the recording PCA/RMT fixtures and synchronous executor dispatch from
// the production-driver harness. No runtime mock mode is enabled.
#define main output_driver_regression_main
#include "output_drivers_test.cpp"
#undef main

#include "ConfigStore.hpp"
#include "SceneManager.hpp"
#include "PowerBudget.hpp"
#include "ManualControlTypes.hpp"
#include "ControlSourceManager.hpp"
#include "ColorPipeline.hpp"
#include "OutputScheduler.hpp"
// Drive one frame without a host background rendering task. Access control is
// relaxed only in this test translation unit; implementation remains unmodified.
#define private public
#include "LightingRuntime.hpp"
#undef private

namespace {
lg::DeviceConfig stored_config;
std::function<void()> config_load_boundary;
}
namespace lg {
esp_err_t ConfigStore::load_config(DeviceConfig& cfg, bool) {
    // Run only once: LightingRuntime::start and SceneManager::init both load
    // configuration, but the test pauses the first publication boundary.
    if (config_load_boundary) {
        auto boundary = std::move(config_load_boundary);
        config_load_boundary = {};
        boundary();
    }
    cfg = stored_config;
    return ESP_OK;
}
esp_err_t ConfigStore::load_scene(uint8_t, SceneConfig& scene, bool) { scene = SceneConfig{}; scene.global_brightness = 255; return ESP_OK; }
esp_err_t ConfigStore::load_scene_motion(uint8_t, SceneMotionConfig& motion) { motion.reset(); return ESP_OK; }
esp_err_t ConfigStore::load_active_state(uint8_t& index) { index = 0; return ESP_OK; }
esp_err_t ConfigStore::save_active_state(uint8_t) { return ESP_OK; }
esp_err_t ConfigStore::validate_config(const DeviceConfig&, ConfigValidationResult*) { return ESP_OK; }
namespace board { bool is_abnormal_reset_quarantine_active(uint32_t) { return false; } }
}

namespace {
struct LightingFixture {
    ControllerFixture output;
    ControlSourceManager sources;
    ConfigStore config_store;
    std::unique_ptr<LightingRuntime> runtime;
    explicit LightingFixture(uint8_t addressable = 0, uint8_t pwm = 1) {
        stored_config = config(addressable, pwm, 4);
        stored_config.max_power_mw = 0;
        stored_config.soft_start_enabled = false;
        stored_config.gamma_x10 = 10;
        output.start(stored_config);
        require(sources.init() == ESP_OK, "source init failed");
        runtime = std::make_unique<LightingRuntime>(sources, output.controller, config_store);
        require(runtime->start() == ESP_OK, "lighting start failed");
    }
    FrameResult render() { return runtime->render_frame(static_cast<uint32_t>(host_now_us.load() / 1000)); }
};

void scheduler_rejects_stale_topology() {
    for (unsigned change = 0; change < 7; ++change) {
        ControllerFixture f;
        const auto rendered_cfg = config(1, 1, 4);
        auto current_cfg = rendered_cfg;
        if (change == 0 || change == 6) current_cfg.channels[0].pixel_count = 2;
        if (change == 1) current_cfg = config(2, 1, 4);
        if (change == 2) {
            current_cfg.channels[0].addressable_type = AddressableType::SK6812;
            current_cfg.channels[0].pixel_order = PixelOrder::GRBW;
        }
        if (change == 3) current_cfg.channels[0].pixel_order = PixelOrder::RGB;
        if (change == 4) {
            current_cfg.channels[8].enabled = false;
            current_cfg.channels[9].enabled = true;
        }
        f.start(current_cfg);
        if (change >= 5) f.controller.set_pwm_available(false);
        std::array<std::vector<uint8_t>, 8> staged;
        for (uint8_t lane = 0; lane < 8; ++lane) {
            if (!f.controller.pixels().configured(lane)) continue;
            f.controller.pixels().fill(lane, 11, 22, 33, 44);
            const auto* wire = f.controller.pixels().wire_data(lane);
            staged[lane].assign(wire, wire + f.controller.pixels().rmt().wire_len(lane));
        }
        f.controller.pwm().set_all(789);
        auto frame = std::make_unique<RenderFrame>();
        frame->channels[0].pixels.fill({200, 201, 202, 203});
        frame->channels[8].pwm = 3000;
        const unsigned writes = f.pca.frames;
        const size_t rmt_events = f.rmt.events.size();
        const auto rejected = OutputScheduler::schedule(*frame, rendered_cfg, f.controller);
        const bool no_commit = f.pca.frames == writes && f.rmt.events.size() == rmt_events &&
                               f.controller.commit_metrics().attempted == 0;
        bool unchanged = std::all_of(f.controller.pwm().values().begin(), f.controller.pwm().values().end(),
                                     [](uint16_t value) { return value == 789; });
        for (uint8_t lane = 0; lane < 8; ++lane) {
            if (!staged[lane].empty()) {
                unchanged &= std::equal(staged[lane].begin(), staged[lane].end(), f.controller.pixels().wire_data(lane));
            }
        }
        if (change >= 5) f.controller.set_pwm_available(true);
        const esp_err_t expected = change >= 5 ? ESP_ERR_INVALID_STATE : kErrStaleOutputFrame;
        require(rejected == expected && no_commit && unchanged,
                "stale topology/unavailable PWM was acknowledged or mutated buffers before rejection");
        require(OutputScheduler::schedule(*frame, current_cfg, f.controller) == ESP_OK,
                "current-topology frame could not resume after stale input rejection");
    }
}

void scheduler_invalid_frame_atomic() {
    ControllerFixture f;
    const auto cfg = config(1, 1, 4);
    f.start(cfg);
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto candidate = cfg;
        auto frame = std::make_unique<RenderFrame>();
        frame->channels[0].pixels.fill({190, 191, 192, 0});
        frame->channels[8].pwm = invalid == 0 ? 4096 : 500;
        if (invalid == 1) candidate.channels[0].pixel_order = PixelOrder::RGBW;
        if (invalid == 2) candidate.channels[0].pixel_count = 0;
        if (invalid == 3) candidate.channels[0].pixel_count = MAX_PIXELS + 1;
        f.controller.pixels().fill(0, 7, 8, 9);
        f.controller.pwm().set(0, 333);
        const auto* wire = f.controller.pixels().wire_data(0);
        const std::vector<uint8_t> before(wire, wire + 12);
        const unsigned writes = f.pca.frames;
        const size_t events = f.rmt.events.size();
        require(OutputScheduler::schedule(*frame, candidate, f.controller) == ESP_ERR_INVALID_ARG,
                "invalid PWM/pixel topology was silently accepted");
        require(f.pca.frames == writes && f.rmt.events.size() == events && f.controller.pwm().values()[0] == 333 &&
                std::equal(before.begin(), before.end(), wire), "invalid input changed staged or physical output");
    }
    auto frame = std::make_unique<RenderFrame>();
    frame->channels[8].pwm = 700;
    frame->channels[9].pwm = UINT16_MAX; // Disabled channel data is deliberately ignored.
    require(OutputScheduler::schedule(*frame, cfg, f.controller) == ESP_OK && f.pca.duties[0] == 700 && f.pca.duties[1] == 0,
            "disabled-channel input affected the active PWM channel");
}

void scheduler_reconfigure_race() {
    {
        ControllerFixture f;
        const auto rendered_cfg = config(1, 1, 4);
        f.start(rendered_cfg);
        auto frame = std::make_unique<RenderFrame>();
        frame->channels[0].pixels.fill({80, 90, 100, 0});
        frame->channels[8].pwm = 900;
        std::promise<void> awaiting_transaction;
        auto arrived = awaiting_transaction.get_future();
        f.controller.lock();
        auto scheduled = std::async(std::launch::async, [&] {
            host_before_take = [&](SemaphoreHandle_t) { awaiting_transaction.set_value(); };
            return OutputScheduler::schedule(*frame, rendered_cfg, f.controller);
        });
        const bool queued = arrived.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        auto next = rendered_cfg;
        next.channels[0].pixel_count = 2;
        const esp_err_t changed = f.controller.reconfigure(next);
        f.controller.unlock();
        const esp_err_t result = scheduled.get();
        require(queued && changed == ESP_OK && result == kErrStaleOutputFrame &&
                f.controller.commit_metrics().successful == 0,
                "topology validation ran before acquiring the staging transaction or acknowledged a stale frame");
        require(OutputScheduler::schedule(*frame, next, f.controller) == ESP_OK,
                "fresh configuration could not render after the concurrent topology change");
    }

    LightingFixture f(1, 1);
    require(f.render() == FrameResult::Committed, "runtime could not establish its initial topology");
    uint32_t generation = 0;
    require(f.runtime->activate_scene_checked(1, &generation) && f.runtime->scene_apply_pending(),
            "could not establish a pending scene generation");
    bool swapped = false;
    unsigned writes_after_change = 0;
    const auto attempted = f.output.controller.commit_metrics().attempted;
    host_after_give_target = f.runtime->mutex_;
    host_after_give = [&](SemaphoreHandle_t) {
        stored_config.channels[0].pixel_count = 2;
        swapped = f.output.controller.reconfigure(stored_config) == ESP_OK;
        f.runtime->reload_device_config();
        writes_after_change = f.output.pca.frames;
    };
    require(f.render() == FrameResult::NoOutput && swapped && f.output.pca.frames == writes_after_change &&
            f.output.controller.commit_metrics().attempted == attempted &&
            !f.runtime->is_output_fault() && f.runtime->output_fault_count() == 0 &&
            f.runtime->scene_apply_pending() && f.runtime->applied_scene_generation() < generation,
            "stale runtime frame was committed, fault-latched, or acknowledged after topology changed");
    require(f.render() == FrameResult::Committed && !f.runtime->scene_apply_pending() &&
            f.runtime->applied_scene_generation() == generation && !f.runtime->is_output_fault(),
            "runtime did not retry and acknowledge the pending scene using fresh topology");
    f.output.controller.set_pwm_available(false);
    require(f.render() == FrameResult::Failed && f.runtime->is_output_fault(),
            "actual PWM unavailability was treated as a recoverable stale frame");
    f.output.controller.set_pwm_available(true);
}

void power_shared_model() {
    ControllerFixture f;
    auto cfg = config(3, 1, 1);
    cfg.channels[8].rated_full_power_mw = 4095;
    f.start(cfg);
    PowerBudget budget;
    auto frame = std::make_unique<RenderFrame>();
    frame->channels[0].pixels[0] = {255, 255, 255, 255}; // RGB wire ignores W.
    frame->channels[1].pixels[0] = {255, 255, 255, 255};
    frame->channels[8].pwm = 2048;
    require(budget.enforce(*frame, cfg, 1000) == 4208, "RGB/RGBW/PWM models disagree with conservative guard assumptions");
    const auto request = budget.output_request();
    require(OutputScheduler::schedule(*frame, cfg, f.controller, &request) == ESP_OK, "shared-model frame failed");
    const auto output = f.controller.power_diagnostics();
    require(output.command_confirmed && output.estimate_complete && output.limited_power_mw == 4208 &&
            output.requested_power_mw == 4208, "unlimited emitted power differs from renderer model");
    require(f.rmt.channels[0].frame == std::vector<uint8_t>({255, 255, 255}) &&
            f.rmt.channels[1].frame == std::vector<uint8_t>({255, 255, 255, 255}) && f.pca.duties[0] == 2048,
            "unlimited model changed output bytes");

    // Preserve fractional demand until the final report: 10 mW idle plus
    // 295/255 mW red and 1000/4095 mW PWM rounds upward to 12 mW.
    cfg.channels[8].rated_full_power_mw = 1000;
    f.controller.set_power_config(cfg);
    *frame = RenderFrame{};
    frame->channels[0].pixels[0] = {1, 0, 0, 0};
    frame->channels[8].pwm = 1;
    require(budget.enforce(*frame, cfg, 1020) == 12, "small loads were rounded away before aggregation");
    require(OutputScheduler::schedule(*frame, cfg, f.controller) == ESP_OK &&
            f.controller.power_diagnostics().limited_power_mw == 12, "fractional final guard model differs");

    cfg.channels[8].rated_full_power_mw = 0;
    cfg.channels[8].max_milliwatts = 999999; // Canonical v4 zero remains unknown.
    f.controller.set_power_config(cfg);
    budget.enforce(*frame, cfg, 1040);
    require(OutputScheduler::schedule(*frame, cfg, f.controller) == ESP_OK, "unknown-rated PWM frame failed");
    require(!budget.estimate_complete() && !f.controller.power_diagnostics().estimate_complete &&
            f.controller.power_diagnostics().unmodeled_channel_mask == (1u << 8), "unknown PWM rating silently became modeled");
}

void power_idle_floors() {
    for (unsigned tier = 0; tier < 3; ++tier) {
        for (uint32_t ceiling : {19u, 20u, 21u}) {
            ControllerFixture f;
            auto cfg = config(1, 0, 4); // Fixed 20 mW idle, 1180 mW active red.
            if (tier == 0) cfg.channels[0].channel_limit_mw = ceiling;
            if (tier == 1) {
                auto& psu = cfg.power_supplies[0];
                psu.enabled = true; psu.channel_mask = 1;
                psu.voltage_mv = ceiling * 1000; psu.rated_current_ma = 1;
                psu.usable_capacity_permille = 1000;
            }
            if (tier == 2) cfg.max_power_mw = ceiling;
            f.start(cfg);
            // Exercise the independent final guard with an unbudgeted source.
            f.controller.pixels().fill(0, 255, 0, 0);
            require(f.controller.try_commit() == ESP_OK, "tiny-budget guard rejected the output transaction");
            require(std::all_of(f.rmt.channels[0].frame.begin(), f.rmt.channels[0].frame.end(),
                                [](uint8_t byte) { return byte == 0; }), "fixed idle was scaled and left active demand above the ceiling");
            auto output = f.controller.power_diagnostics();
            require(output.requested_power_mw == 1200 && output.limited_power_mw == 20 && output.idle_power_mw == 20,
                    "guard replaced the non-dimmable idle floor with the configured limit");

            PowerBudget budget;
            budget.set_limit_mw(cfg.max_power_mw);
            auto frame = std::make_unique<RenderFrame>();
            for (unsigned p = 0; p < 4; ++p) frame->channels[0].pixels[p] = {255, 0, 0, 0};
            require(budget.enforce(*frame, cfg, 1000) == 1200 && budget.limited_power_mw() == 20,
                    "renderer lost requested demand or scaled away fixed idle");
            const auto request = budget.output_request();
            require(OutputScheduler::schedule(*frame, cfg, f.controller, &request) == ESP_OK,
                    "renderer/guard idle-floor frame failed");
            output = f.controller.power_diagnostics();
            require(output.limited_power_mw == 20 && output.throttling_events == 2,
                    "shared renderer/guard limiting was counted twice or misreported");
            if (tier == 1) require(output.supply_domains[0].estimated_load_mw == 20,
                                   "supply report hid an impossible idle floor");
            require(f.controller.blackout() == ESP_OK && f.controller.power_diagnostics().command_confirmed &&
                    f.controller.power_diagnostics().limited_power_mw == 20,
                    "impossible power limit blocked blackout or fabricated zero consumption");
        }
    }
}

void power_limit_chain() {
    ControllerFixture f;
    auto cfg = config(3, 1, 1);
    cfg.channels[0].channel_limit_mw = 500;
    cfg.channels[8].rated_full_power_mw = 4095;
    cfg.max_power_mw = 1200;
    auto& shared = cfg.power_supplies[0];
    shared.enabled = true; shared.channel_mask = 1u | (1u << 8);
    shared.voltage_mv = 5000; shared.rated_current_ma = 200; shared.usable_capacity_permille = 1000;
    auto& independent = cfg.power_supplies[1];
    independent.enabled = true; independent.channel_mask = 2;
    independent.voltage_mv = 5000; independent.rated_current_ma = 140; independent.usable_capacity_permille = 1000;
    f.start(cfg);
    auto frame = std::make_unique<RenderFrame>();
    frame->channels[0].pixels[0] = {255, 255, 255, 0};
    frame->channels[1].pixels[0] = {255, 0, 0, 255};
    frame->channels[8].pwm = 4095;
    PowerBudget budget;
    budget.set_limit_mw(1200);
    budget.observe_output_power(1200); // Steady-state budget, no enable transient.
    require(budget.enforce(*frame, cfg, 1000) == 5665, "requested demand was captured after a local clamp");
    require(frame->channels[0].pixels[0].r == 21 && frame->channels[1].pixels[0].r == 182 &&
            frame->channels[1].pixels[0].w == 182 && frame->channels[8].pwm == 628,
            "local/domain/global chain used stale or scaled-idle accounting");
    const auto renderer = budget.diagnostics();
    require(renderer.limited_power_mw == 1193 && renderer.supply_domains[0].estimated_load_mw == 706 &&
            renderer.supply_domains[1].estimated_load_mw == 487, "renderer supply loads were sampled before final scaling");
    const auto request = budget.output_request();
    require(OutputScheduler::schedule(*frame, cfg, f.controller, &request) == ESP_OK, "chained power frame failed");
    require(f.rmt.channels[0].frame == std::vector<uint8_t>({21, 21, 21}) &&
            f.rmt.channels[1].frame == std::vector<uint8_t>({0, 182, 0, 182}) && f.pca.duties[0] == 628,
            "final guard dimmed an already valid quantized frame again");
    const auto output = f.controller.power_diagnostics();
    require(output.requested_power_mw == 5665 && output.limited_power_mw == 1193 &&
            output.supply_domains[0].estimated_load_mw == 706 && output.supply_domains[1].estimated_load_mw == 487,
            "final output report differs from emitted mixed-channel commands");
    require(output.throttling_events == 1 && output.channel_throttle_events == 1 &&
            output.supply_domains[0].throttle_events == 1 && output.supply_domains[1].throttle_events == 0,
            "renderer and final guard duplicated or lost limiting events");
}

void power_zero_supply() {
    for (bool rounded_zero : {false, true}) {
        ControllerFixture f;
        auto cfg = config(3, 1, 1);
        cfg.channels[8].rated_full_power_mw = 1000;
        auto& psu = cfg.power_supplies[0];
        psu.enabled = true; psu.channel_mask = 1u | (1u << 8);
        psu.voltage_mv = rounded_zero ? 3000 : 5000;
        psu.rated_current_ma = rounded_zero ? 1 : 200;
        psu.usable_capacity_permille = rounded_zero ? 1 : 0;
        require(psu.usable_power_mw() == 0, "zero-capacity fixture is not zero");
        f.start(cfg);
        f.controller.pixels().fill(0, 255, 0, 0);
        f.controller.pixels().fill(1, 255, 0, 0);
        f.controller.pwm().set(0, 4095);
        require(f.controller.try_commit() == ESP_OK, "zero-capacity final guard failed");
        require(f.rmt.channels[0].frame == std::vector<uint8_t>({0, 0, 0}) && f.pca.duties[0] == 0 &&
                f.rmt.channels[1].frame == std::vector<uint8_t>({0, 255, 0, 0}),
                "zero usable supply acted as unlimited or dimmed an unrelated channel");
        auto output = f.controller.power_diagnostics();
        require(output.requested_power_mw == 1600 && output.limited_power_mw == 305 &&
                output.supply_domains[0].estimated_load_mw == 5, "zero-capacity report erased fixed idle");

        PowerBudget budget;
        auto frame = std::make_unique<RenderFrame>();
        frame->channels[0].pixels[0] = frame->channels[1].pixels[0] = {255, 0, 0, 0};
        frame->channels[8].pwm = 4095;
        require(budget.enforce(*frame, cfg, 1000) == 1600 && budget.limited_power_mw() == 305 &&
                frame->channels[0].pixels[0].r == 0 && frame->channels[8].pwm == 0 &&
                frame->channels[1].pixels[0].r == 255, "renderer zero-capacity policy differs from final guard");
        const auto request = budget.output_request();
        require(OutputScheduler::schedule(*frame, cfg, f.controller, &request) == ESP_OK,
                "zero-capacity renderer/guard frame failed");
        output = f.controller.power_diagnostics();
        require(output.limited_power_mw == 305 && output.supply_domains[0].estimated_load_mw == 5 &&
                output.supply_domains[0].throttle_events == 2, "zero-capacity final report or event merge differs");
    }
    ControllerFixture unknown;
    auto cfg = config(0, 1);
    auto& psu = cfg.power_supplies[0];
    psu.enabled = true; psu.channel_mask = 1u << 8;
    psu.voltage_mv = 5000; psu.rated_current_ma = 200; psu.usable_capacity_permille = 0;
    unknown.start(cfg);
    unknown.controller.pwm().set(0, 4095);
    require(unknown.controller.try_commit() == ESP_OK && unknown.pca.duties[0] == 0 &&
            !unknown.controller.power_diagnostics().estimate_complete,
            "unmodeled PWM escaped the explicit zero-capacity domain");
    PowerBudget budget;
    auto frame = std::make_unique<RenderFrame>();
    frame->channels[8].pwm = 4095;
    budget.enforce(*frame, cfg, 1000);
    require(frame->channels[8].pwm == 0 && !budget.estimate_complete(),
            "renderer treated unmodeled zero-capacity demand as free power");
}

void power_post_guard_snapshot() {
    LightingFixture f(1, 0);
    auto physical_cfg = stored_config;
    physical_cfg.max_power_mw = 500;
    f.output.controller.set_power_config(physical_cfg); // Final guard is authoritative during hot apply.
    ManualOverrideFrame manual{};
    manual.channels[0].active = true;
    manual.channels[0].color = {255, 0, 0, 0};
    manual.channels[0].brightness = 255;
    require(f.runtime->submit_manual_override(manual), "power snapshot manual frame rejected");
    bool callback_checked = false;
    f.output.controller.set_commit_observer([&](esp_err_t err) {
        require(err == ESP_OK, "guarded snapshot commit failed");
        const auto snapshot = f.runtime->power_diagnostics(); // Output lock is held, lighting lock must be free.
        require(snapshot.limited_power_mw == 497 && snapshot.requested_power_mw == 1200,
                "commit callback saw a pre-guard or uncommitted power estimate");
        callback_checked = true;
    });
    require(f.render() == FrameResult::Committed && callback_checked, "guarded runtime frame failed");
    require(f.output.rmt.channels[0].frame[1] == 103 && f.runtime->current_render_frame_.channels[0].pixels[0].r == 255,
            "power telemetry confused emitted brightness with the renderer preview");
    const auto output = f.runtime->power_diagnostics();
    require(output.estimate_complete && output.limit_mw == 500 && output.limited_power_mw == 497 &&
            output.throttling_events == 1, "runtime snapshot ignored final guard limiting");
    require(f.output.controller.blackout() == ESP_OK, "snapshot blackout failed");
    const auto black = f.runtime->power_diagnostics();
    require(black.estimate_complete && black.requested_power_mw == 20 && black.limited_power_mw == 20,
            "blackout retained the bright renderer estimate or erased the idle floor");
}

void power_failed_commit_snapshot() {
    LightingFixture f(0, 1);
    auto cfg = stored_config;
    cfg.channels[8].rated_full_power_mw = 1000;
    f.output.controller.set_power_config(cfg);
    f.output.controller.pwm().set(0, 1000);
    require(f.output.controller.try_commit() == ESP_OK && f.runtime->power_diagnostics().limited_power_mw == 245,
            "could not establish a confirmed power estimate");
    require(f.output.controller.recover_pca() == ESP_OK && f.runtime->power_diagnostics().estimate_complete &&
            f.runtime->power_diagnostics().limited_power_mw == 0,
            "successful PCA recovery retained a stale nonzero PWM estimate");
    f.output.controller.pwm().set(0, 1000);
    require(f.output.controller.try_commit() == ESP_OK, "could not restore the prior command after recovery");
    f.output.controller.pwm().set(0, 3000);
    f.output.pca.fail_frames = 3; // New command, controller blackout, and runtime fault-observer blackout fail.
    require(f.output.controller.try_commit() != ESP_OK, "injected commit/blackout failure was accepted");
    const auto failed = f.runtime->power_diagnostics();
    require(!failed.estimate_complete && failed.model_state == PowerEstimateState::Unknown &&
            failed.limited_power_mw == 245 && failed.requested_power_mw == 245,
            "failed hardware I/O published a new successful power estimate");
    require(f.output.controller.recover_pca() == ESP_OK && f.output.controller.blackout() == ESP_OK,
            "power snapshot recovery failed");
    require(f.runtime->power_diagnostics().estimate_complete && f.runtime->power_diagnostics().limited_power_mw == 0,
            "confirmed PWM blackout did not restore a valid zero estimate");
}

void lighting_case(const std::string& name) {
    if (name == "scheduler_rejects_stale_topology") scheduler_rejects_stale_topology();
    else if (name == "scheduler_invalid_frame_atomic") scheduler_invalid_frame_atomic();
    else if (name == "scheduler_reconfigure_race") scheduler_reconfigure_race();
    else if (name == "controller_rmt_failure_skips_pwm") controller_rmt_failure_skips_pwm();
    else if (name == "transaction_cross_controller_move") transaction_cross_controller_move();
    else if (name == "power_shared_model") power_shared_model();
    else if (name == "power_idle_floors") power_idle_floors();
    else if (name == "power_limit_chain") power_limit_chain();
    else if (name == "power_zero_supply") power_zero_supply();
    else if (name == "power_post_guard_snapshot") power_post_guard_snapshot();
    else if (name == "power_failed_commit_snapshot") power_failed_commit_snapshot();
    else if (name == "stop_repeated_restart") {
        LightingFixture f;
        f.runtime->stop();
        f.runtime->stop();
        require(f.runtime->lifecycle_state() == LightingRuntime::LifecycleState::Stopped,
                "second stop changed a stopped instance back to Stopping");
        require(f.runtime->start() == ESP_OK, "cleanly stopped instance could not restart");
    } else if (name == "restart_snapshot_excludes_initialization") {
        LightingFixture f;
        f.runtime->stop();
        std::strcpy(stored_config.device_name, "Restarted configuration");
        stored_config.gamma_x10 = 24;
        stored_config.watchdog_timeout_s = 777;
        stored_config.max_power_mw = 12345;

        std::promise<void> reader_arrived;
        auto arrived = reader_arrived.get_future();
        std::future<DeviceConfig> reader;
        bool boundary_seen = false, reader_queued = false;
        bool reader_finished_during_init = false, correct_mutex = false;
        config_load_boundary = [&] {
            boundary_seen = true;
            reader = std::async(std::launch::async, [&] {
                host_before_take = [&](SemaphoreHandle_t sem) {
                    correct_mutex = sem == f.runtime->mutex_;
                    reader_arrived.set_value();
                };
                return f.runtime->get_config_snapshot();
            });
            reader_queued = arrived.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            reader_finished_during_init =
                reader.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
            // Do not wait for the snapshot here: correct production code holds
            // the shared mutex until configuration initialization is complete.
        };
        const esp_err_t started = f.runtime->start();
        const DeviceConfig observed = reader.get();
        require(started == ESP_OK && boundary_seen && reader_queued && correct_mutex,
                "restart snapshot did not reach the shared configuration boundary");
        require(!reader_finished_during_init,
                "configuration snapshot escaped the mutex during restart initialization");
        require(std::strcmp(observed.device_name, stored_config.device_name) == 0 &&
                observed.gamma_x10 == stored_config.gamma_x10 &&
                observed.watchdog_timeout_s == stored_config.watchdog_timeout_s &&
                observed.max_power_mw == stored_config.max_power_mw,
                "restart snapshot did not observe the complete newly loaded configuration");
    } else if (name == "restart_discards_frozen_transition") {
        LightingFixture f;
        SceneConfig stale_target{};
        std::strcpy(stale_target.name, "Do not resume");
        stale_target.global_brightness = 17;
        f.runtime->activate_scene_direct(stale_target, false);
        require(f.runtime->freeze_transition(), "could not freeze the pre-stop transition");
        f.runtime->set_manual_transition_progress(12345);
        auto& scene = f.runtime->scene_manager();
        require(scene.is_transitioning() && scene.is_manual_transition() && scene.is_transition_frozen(),
                "test did not establish a paused manual transition");

        f.runtime->stop();
        require(f.runtime->start() == ESP_OK, "restart with a frozen transition failed");
        require(!scene.is_transitioning() && !scene.is_manual_transition() && !scene.is_transition_frozen(),
                "restart retained the old manual or frozen transition state");
        require(scene.transition_progress_q15() == 32768,
                "restart retained progress from the previous transition");
        require(std::strcmp(scene.target_scene().name, scene.current_scene().name) == 0 &&
                scene.target_scene().global_brightness == scene.current_scene().global_brightness &&
                scene.current_scene().global_brightness == 255,
                "restart retained the previous target instead of the newly loaded scene");
        require(f.render() == FrameResult::Committed &&
                std::strcmp(f.runtime->current_render_scene_.name, stale_target.name) != 0 &&
                f.runtime->current_render_scene_.global_brightness == 255,
                "the first restarted frame resumed the old transition target or progress");
    } else if (name == "new_scene_replaces_frozen_transition") {
        LightingFixture f;
        auto& manager = f.runtime->scene_manager();
        SceneConfig previous{};
        previous.global_brightness = 30;
        manager.activate_scene_direct(previous, 1000, false);
        manager.process_frame(1250);
        require(manager.freeze_transition(), "replacement fixture could not freeze its old cue");

        SceneConfig replacement{};
        std::strcpy(replacement.name, "Replacement");
        replacement.global_brightness = 201;
        replacement.transition_ms = 200;
        manager.activate_scene_direct(replacement, 1300, false);
        require(manager.is_transitioning() && !manager.is_manual_transition() && !manager.is_transition_frozen(),
                "new timed scene inherited frozen/manual mode");
        manager.process_frame(1400);
        require(manager.transition_progress_q15() > 0, "replacement remained at zero progress");
        const auto& completed = manager.process_frame(1500);
        require(!manager.is_transitioning() && completed.global_brightness == 201 &&
                std::strcmp(completed.name, "Replacement") == 0, "replacement did not reach its new target");

        manager.activate_scene_direct(previous, 1600, false);
        require(manager.freeze_transition(), "immediate fixture could not freeze");
        manager.activate_scene_direct(replacement, 1700, true);
        require(!manager.is_transitioning() && !manager.is_manual_transition() && !manager.is_transition_frozen(),
                "immediate scene retained stale transition controls");
        require(manager.process_frame(1700).global_brightness == 201, "immediate replacement did not render");

        manager.activate_scene_direct(previous, 1800, false);
        require(manager.freeze_transition() && manager.prepare_manual_transition(2), "new manual fixture failed");
        require(manager.is_manual_transition() && !manager.is_transition_frozen(),
                "new T-Bar transition inherited the old cue's frozen flag");
    } else if (name == "preset_and_watchdog_replace_manual_transition") {
        LightingFixture f;
        auto& manager = f.runtime->scene_manager();
        require(manager.prepare_manual_transition(2), "manual preset fixture failed");
        manager.set_manual_transition_progress(10000);
        require(manager.freeze_transition(), "manual preset could not freeze");
        require(manager.activate_scene_custom(3, 200, TransitionCurve::Linear, 1400), "replacement preset rejected");
        require(!manager.is_manual_transition() && !manager.is_transition_frozen(),
                "replacement preset retained stale transition controls");
        manager.process_frame(1600);
        require(!manager.is_transitioning() && manager.active_scene_index() == 3, "replacement preset did not complete");

        auto cfg = stored_config;
        cfg.watchdog_timeout_s = 1;
        cfg.fallback_preset_index = 1;
        manager.update_device_config(cfg);
        require(manager.prepare_manual_transition(2), "watchdog manual fixture failed");
        manager.set_manual_transition_progress(15000);
        require(manager.freeze_transition(), "watchdog fixture could not freeze");
        manager.process_frame(4000);
        require(manager.is_fallback_active() && manager.active_scene_index() == 1 &&
                !manager.is_manual_transition() && !manager.is_transition_frozen(),
                "watchdog fallback inherited manual/frozen mode");
        manager.process_frame(5000);
        require(!manager.is_transitioning() && manager.transition_progress_q15() == 32768,
                "watchdog fallback remained stuck instead of reaching its safe scene");
    } else if (name == "stop_timeout_retains_task") {
        LightingFixture f;
        const auto owner = f.runtime->task_handle_;
        host_task_ack = false;
        f.runtime->stop();
        require(f.runtime->lifecycle_state() == LightingRuntime::LifecycleState::Faulted &&
                f.runtime->task_handle_ == owner && f.runtime->mutex_, "stop timeout discarded live task resources");
        require(f.runtime->start() == ESP_ERR_INVALID_STATE, "restart admitted over an unacknowledged task");
        host_task_ack = true;
        f.runtime->stop();
        require(f.runtime->lifecycle_state() == LightingRuntime::LifecycleState::Stopped, "second stop did not finish ownership cleanup");
    } else if (name == "stop_waiting_producer" || name == "stop_restart_waiting_producer") {
        LightingFixture f;
        std::mutex mutex;
        std::condition_variable condition;
        bool entered = false, released = false;
        auto producer = std::async(std::launch::async, [&] {
            host_before_take = [&](SemaphoreHandle_t sem) {
                require(sem == f.runtime->mutex_, "paused at wrong semaphore");
                std::unique_lock<std::mutex> lock(mutex);
                entered = true; condition.notify_all();
                condition.wait(lock, [&] { return released; });
            };
            ManualOverrideFrame frame{}; frame.channels[8].active = true; frame.channels[8].pwm = 3000;
            return f.runtime->submit_manual_override(frame);
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            require(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }), "producer did not queue");
        }
        f.runtime->stop();
        if (name == "stop_restart_waiting_producer") {
            require(f.runtime->start() == ESP_OK, "restart failed before admitting old waiter");
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true; condition.notify_all();
        }
        const bool accepted = producer.get();
        require(host_unsafe_deletes == 0, "stop deleted the mutex while a producer was already waiting");
        require(!accepted, "an admitted producer repopulated output ownership after stop");
        if (name == "stop_waiting_producer") {
            require(f.runtime->start() == ESP_OK, "restart failed after rejecting the late producer");
        }
        require(!f.runtime->manual_control_status().active, "late producer leaked into the next lifecycle");
    } else if (name == "live_short_frame_tail") {
        LightingFixture f(3, 0);
        auto frame = std::make_unique<LiveDmxFrame>();
        for (unsigned ch = 0; ch < 2; ++ch) {
            auto& live = frame->channels[ch];
            live.active = true; live.opacity = 255;
            live.timestamp_ms = host_now_us.load() / 1000;
            live.pixel_count = ch == 0 ? 4 : 1;
            for (unsigned p = 0; p < live.pixel_count; ++p) live.pixels[p] = ch == 0 ? LightValue{200, 0, 0} : LightValue{0, 0, 200};
        }
        require(f.runtime->submit_live_frame(*frame), "live frame rejected");
        require(f.render() == FrameResult::Committed, "live frame did not commit");
        for (unsigned p = 1; p < 4; ++p) {
            const auto& pixel = f.runtime->current_render_frame_.channels[1].pixels[p];
            require(pixel.r == 0 && pixel.g == 0 && pixel.b == 0 && pixel.w == 0,
                    "short live channel inherited tail pixels from a different channel");
        }
    } else if (name == "manual_frame_snapshot") {
        LightingFixture f;
        ManualOverrideFrame first{}, next{};
        first.channels[8].active = next.channels[8].active = true;
        first.channels[8].pwm = 1000; next.channels[8].pwm = 3000;
        require(f.runtime->submit_manual_override(first), "first manual frame rejected");
        host_after_give_target = f.runtime->mutex_;
        host_after_give = [&](SemaphoreHandle_t sem) {
            require(sem == f.runtime->mutex_, "wrong snapshot boundary");
            require(f.runtime->submit_manual_override(next), "next manual frame rejected");
        };
        require(f.render() == FrameResult::Committed, "manual frame did not commit");
        const auto first_duty = ColorPipeline::to_pwm({}, 1000, stored_config.channels[8], 255);
        const auto next_duty = ColorPipeline::to_pwm({}, 3000, stored_config.channels[8], 255);
        require(f.output.pca.duties[0] == first_duty, "current render consumed manual state written after its snapshot boundary");
        require(f.render() == FrameResult::Committed && f.output.pca.duties[0] == next_duty,
                "next complete manual frame was not rendered");
    } else if (name == "new_estop_during_reset_blackout") {
        LightingFixture f;
        f.runtime->trigger_estop();
        f.runtime->clear_estop();
        bool trip_once = true;
        f.runtime->set_preview_callback([&](const RenderFrame&, const DeviceConfig&) {
            if (trip_once) { trip_once = false; f.runtime->trigger_estop(); }
        });
        f.render();
        f.render();
        require(f.runtime->is_estop_active() && f.output.controller.is_estop_latched(),
                "an older reset frame acknowledged a newly triggered emergency stop");
        require_disabled("new emergency stop was exposed by an older reset");
    } else if (name == "new_estop_before_hardware_clear") {
        LightingFixture f;
        f.runtime->trigger_estop();
        f.runtime->clear_estop();
        require(f.render() == FrameResult::EstopBlackout, "first reset boundary did not blackout");
        host_before_take = [&](SemaphoreHandle_t) { f.runtime->trigger_estop(); };
        f.render();
        require(f.runtime->is_estop_active() && f.output.controller.is_estop_latched(),
                "queued hardware clear acknowledged a stop newer than the user's reset request");
        require_disabled("newer emergency stop was released before hardware clear");
    } else if (name == "normal_estop_clear_requires_two_frames") {
        LightingFixture f;
        f.runtime->trigger_estop();
        f.runtime->clear_estop();
        require(f.render() == FrameResult::EstopBlackout && f.output.controller.is_estop_latched(),
                "first reset boundary released the hardware latch");
        require(f.render() == FrameResult::EstopBlackout && !f.runtime->is_estop_active() && !f.output.controller.is_estop_latched(),
                "second reset boundary did not complete the explicit clear");
        require_disabled("successful reset exposed hardware before a new output commit");
        require(f.render() == FrameResult::Committed, "output did not resume after explicit two-frame reset");
    } else if (name == "armed_requires_a_commit") {
        LightingFixture f;
        f.runtime->report_output_fault(ESP_FAIL);
        require(f.runtime->clear_output_fault() == ESP_OK, "fault could not be armed");
        require(f.sources.acquire(ControlSource::Factory), "factory ownership rejected");
        require(f.render() == FrameResult::NoOutput, "lighting rendered over factory ownership");
        require(f.runtime->output_state() == LightingRuntime::OutputState::Armed,
                "output fault was declared healthy without a successful frame commit");
    } else if (name == "reload_callback_can_clear_live") {
        LightingFixture f;
        bool notified = false;
        f.runtime->set_config_reload_callback([&](const DeviceConfig&) {
            // The actual runtime callback updates DmxRouter, which calls
            // clear_live_frame when the patch changes. Its nonrecursive mutex
            // must be available here, including to concurrent DMX submissions.
            const bool available = std::async(std::launch::async, [&] {
                if (xSemaphoreTake(f.runtime->mutex_, 0) != pdTRUE) return false;
                xSemaphoreGive(f.runtime->mutex_); return true;
            }).get();
            require(available, "config callback reentered the runtime while its nonrecursive mutex was held");
            f.runtime->clear_live_frame();
            notified = true;
        });
        f.runtime->reload_device_config();
        require(notified, "reload did not notify the DMX configuration callback");
    } else require(false, "unknown lighting case");
}
}

int main(int argc, char** argv) {
    require(argc == 2, "one lighting case is required");
    try { lighting_case(argv[1]); std::cout << "PASS " << argv[1] << '\n'; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL " << argv[1] << ": " << error.what() << '\n'; return 1; }
}
