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

void lighting_case(const std::string& name) {
    if (name == "stop_repeated_restart") {
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
