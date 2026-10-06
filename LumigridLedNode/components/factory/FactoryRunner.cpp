#include "FactoryRunner.hpp"
#include "FactoryApp.hpp"
#include "ChannelHardware.hpp"
#include "JobManager.hpp"
#include "RuntimeMemory.hpp"

#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>

namespace lg {
namespace {

constexpr FactoryEffect kLoopEffects[] = {
    FactoryEffect::Identity,
    FactoryEffect::PixelChase,
    FactoryEffect::Rainbow,
    FactoryEffect::WhiteWalk,
    FactoryEffect::PwmWalk,
    FactoryEffect::PwmFade,
    FactoryEffect::CombinedPulse
};

constexpr uint32_t kLoopEffectDurationMs = 10'000;
constexpr uint32_t kInterEffectBlackoutMs = 300;
constexpr uint32_t kFramePeriodMs = 40;
constexpr UBaseType_t kQueueDepth = 8;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

// LG-035 §9: Explicit factory output safety limits.
// Normal diagnostics capped at 96/255. Burn-in defaults to 64/255 and is capped at 96/255.
uint8_t clamp_brightness(uint8_t brightness, bool is_burn_in = false) {
    uint8_t cap = is_burn_in ? FactoryRunner::kBurnInAbsoluteMaxBrightness : FactoryRunner::kNormalDiagnosticMaxBrightness;
    return std::min<uint8_t>(brightness, cap);
}

uint32_t clamp_duration(uint32_t duration_ms, bool is_burn_in = false) {
    if (is_burn_in) {
        constexpr uint32_t kMaxBurnInDurationMs = 24 * 60 * 60 * 1000;
        if (duration_ms == 0) return 60 * 60 * 1000;
        return std::min(duration_ms, kMaxBurnInDurationMs);
    }
    if (duration_ms == 0) {
        return LG_FACTORY_MAX_TEST_MS;
    }
    return std::min(duration_ms, LG_FACTORY_MAX_TEST_MS);
}

} // namespace

FactoryRunner::FactoryRunner(ControlSourceManager& source_manager, OutputController& controller)
    : source_manager_(source_manager), controller_(controller) {
    shutdown_sem_ = xSemaphoreCreateBinary();
}

FactoryRunner::~FactoryRunner() {
    stop();
    // Destruction is terminal: if bounded stop timed out, wait for the
    // cooperative task ACK before deleting the semaphore it uses.
    if (task_handle_) {
        if (shutdown_sem_) {
            xSemaphoreTake(shutdown_sem_, portMAX_DELAY);
        }
        task_handle_ = nullptr;
        controller_.blackout();
        if (status_mutex_) {
            vSemaphoreDelete(status_mutex_);
            status_mutex_ = nullptr;
        }
        if (queue_) {
            vQueueDelete(queue_);
            queue_ = nullptr;
        }
        lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
    }
    if (shutdown_sem_) {
        vSemaphoreDelete(shutdown_sem_);
        shutdown_sem_ = nullptr;
    }
}

esp_err_t FactoryRunner::start() {
    LifecycleState current = lifecycle_state_.load(std::memory_order_acquire);
    if (current == LifecycleState::Running ||
        current == LifecycleState::Starting ||
        current == LifecycleState::Stopping) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current == LifecycleState::Faulted && task_handle_ != nullptr) {
        ESP_LOGW("FactoryRunner", "start() rejected: previous factory task has not terminated");
        return ESP_ERR_INVALID_STATE;
    }
    lifecycle_state_.store(LifecycleState::Starting, std::memory_order_release);
    shutting_down_.store(false, std::memory_order_release);

    if (shutdown_sem_ == nullptr) {
        shutdown_sem_ = xSemaphoreCreateBinary();
    }

    queue_ = xQueueCreate(kQueueDepth, sizeof(FactoryCommand));
    if (queue_ == nullptr) {
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        source_manager_.release(ControlSource::Factory);
        note_factory_task_start_failure();
        return ESP_ERR_NO_MEM;
    }

    status_mutex_ = xSemaphoreCreateMutex();
    if (status_mutex_ == nullptr) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        source_manager_.release(ControlSource::Factory);
        note_factory_task_start_failure();
        return ESP_ERR_NO_MEM;
    }

    status_.active_effect = FactoryEffect::SafeBlackout;
    status_.brightness = LG_FACTORY_DEFAULT_BRIGHTNESS;
    status_.auto_loop = false;
    status_.outputs_enabled = false;
    status_.test_active = false;
    status_.command_rejections = 0;
    status_.commit_failures = 0;
    status_.active_channel = 0xFF;
    status_.session = FactoryTestSession{};

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry,
        "lg_factory",
        8192,
        this,
        5,
        &task_handle_,
        1
    );

    if (created != pdPASS) {
        vSemaphoreDelete(status_mutex_);
        status_mutex_ = nullptr;
        vQueueDelete(queue_);
        queue_ = nullptr;
        task_handle_ = nullptr;
        source_manager_.release(ControlSource::Factory);
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        note_factory_task_start_failure();
        return ESP_FAIL;
    }

    // LG-038.3 §29/§32: track the factory worker's stack high-water mark.
    task_stack_registry().register_task("lg_factory", 8192);

    lifecycle_state_.store(LifecycleState::Running, std::memory_order_release);
    ESP_LOGI("FactoryRunner", "Factory worker started lazily (8192 B stack)");
    return ESP_OK;
}

bool FactoryRunner::ensure_worker_started() {
    const LifecycleState current = lifecycle_state_.load(std::memory_order_acquire);
    if (current == LifecycleState::Running ||
        current == LifecycleState::Starting) {
        return true;
    }
    if (current == LifecycleState::Stopping) {
        return false;
    }
    // Faulted or Stopped or Constructed: attempt one start. A previous
    // un-terminated task keeps start() refusing; that is surfaced as false.
    return start() == ESP_OK;
}

void FactoryRunner::stop() {
    LifecycleState prev = lifecycle_state_.exchange(LifecycleState::Stopping, std::memory_order_acq_rel);
    if (prev == LifecycleState::Stopping || prev == LifecycleState::Stopped) {
        return;
    }
    shutting_down_.store(true, std::memory_order_release);

    if (task_handle_) {
        xTaskAbortDelay(task_handle_);
        bool ack = false;
        if (shutdown_sem_) {
            ack = (xSemaphoreTake(shutdown_sem_, pdMS_TO_TICKS(1000)) == pdTRUE);
        }
        if (!ack) {
            ESP_LOGE("FactoryRunner", "Shutdown ACK timeout! Factory task did not exit cleanly within 1000ms");
            lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
            return;
        }
        task_handle_ = nullptr;
    }

    blackout();
    if (pending_command_id_ != 0 && journal_) {
        journal_->failed(pending_command_id_, ESP_ERR_INVALID_STATE);
        pending_command_id_ = 0;
    }
    if (status_mutex_) {
        vSemaphoreDelete(status_mutex_);
        status_mutex_ = nullptr;
    }
    if (queue_) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
    // LG-038.3 §19: the worker stays alive until reboot once started; stop()
    // here means runtime shutdown. Drop stack telemetry with it.
    task_stack_registry().unregister_task("lg_factory");
    lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
}

bool FactoryRunner::abort() {
    if (burn_in_active_) {
        if (burn_in_job_mgr_ && burn_in_job_id_ != 0) {
            burn_in_job_mgr_->cancel_job(burn_in_job_id_);
        }
        burn_in_active_ = false;
    }
    if (status_mutex_ != nullptr &&
        xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (status_.session.state == FactoryTestState::Running ||
            status_.session.state == FactoryTestState::WaitingForOperator ||
            status_.session.state == FactoryTestState::Preparing) {
            status_.session.state = FactoryTestState::Aborted;
            status_.session.finished_ms = now_ms();
        }
        xSemaphoreGive(status_mutex_);
    }

    FactoryCommand cmd;
    cmd.type = FactoryCommandType::Abort;
    return enqueue(cmd);
}

bool FactoryRunner::enqueue(const FactoryCommand& command) {
    // LG-038.3 §18: first Factory command starts the lazy worker.
    if (!ensure_worker_started()) {
        if (status_mutex_ != nullptr &&
            xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
            ++status_.command_rejections;
            xSemaphoreGive(status_mutex_);
        }
        return false;
    }
    if (queue_ == nullptr) return false;
    if (xQueueSend(queue_, &command, 0) == pdPASS) return true;
    if (status_mutex_ != nullptr &&
        xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        ++status_.command_rejections;
        xSemaphoreGive(status_mutex_);
    }
    return false;
}

bool FactoryRunner::test_channel(uint8_t channel, const char* mode, uint32_t duration_ms) {
    if (channel >= kChannelCount) return false;
    FactoryCommand cmd;
    cmd.type = FactoryCommandType::ChannelTest;
    cmd.lane_or_channel = channel;
    cmd.duration_ms = (duration_ms == 0) ? 3000 : duration_ms;
    cmd.brightness = clamp_brightness(LG_FACTORY_DEFAULT_BRIGHTNESS, false);
    return enqueue(cmd);
}

FactoryStatus FactoryRunner::status() const {
    FactoryStatus copy{};
    if (status_mutex_ != nullptr &&
        xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
        copy = status_;
        xSemaphoreGive(status_mutex_);
    }
    return copy;
}

bool FactoryRunner::start_test_session(
    FactoryTestKind kind,
    uint8_t channel,
    uint32_t duration_ms,
    uint8_t brightness,
    uint32_t* out_session_id,
    FactorySessionOwner owner,
    const char* cert_test_id
) {
    // LG-038.3 §18/§20: a physical test (Factory or Certification) is a real
    // use of the worker; start it here, not at boot.
    if (!ensure_worker_started()) {
        return false;
    }
    if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Running) {
        return false;
    }

    if (kind == FactoryTestKind::AddressableChannel && channel > 7) {
        return false;
    }
    if (kind == FactoryTestKind::PwmChannel && (channel < 8 || channel > 15)) {
        return false;
    }

    const uint32_t sid = next_session_id_++;
    if (out_session_id) {
        *out_session_id = sid;
    }

    FactoryCommand cmd;
    cmd.type = FactoryCommandType::StartSession;
    cmd.session_id = sid;
    cmd.session_kind = kind;
    cmd.lane_or_channel = channel;
    cmd.duration_ms = duration_ms;
    cmd.brightness = brightness;
    cmd.session_owner = owner;
    if (cert_test_id) {
        strncpy(cmd.session_cert_test_id, cert_test_id, sizeof(cmd.session_cert_test_id) - 1);
        cmd.session_cert_test_id[sizeof(cmd.session_cert_test_id) - 1] = '\0';
    }
    return enqueue(cmd);
}

bool FactoryRunner::confirm_test_session(
    uint32_t session_id,
    bool passed,
    const char* notes
) {
    FactoryCommand cmd;
    cmd.type = FactoryCommandType::ConfirmSession;
    cmd.session_id = session_id;
    cmd.session_passed = passed;
    if (notes) {
        strncpy(cmd.session_notes, notes, sizeof(cmd.session_notes) - 1);
        cmd.session_notes[sizeof(cmd.session_notes) - 1] = '\0';
    }
    return enqueue(cmd);
}

bool FactoryRunner::abort_test_session(uint32_t /*session_id*/) {
    return abort();
}

FactoryValidationReport FactoryRunner::validation_report() const {
    FactoryValidationReport report{};
    if (status_mutex_ != nullptr &&
        xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
        report = validation_report_;
        xSemaphoreGive(status_mutex_);
    }
    return report;
}

void FactoryRunner::reset_validation_report() {
    if (status_mutex_ != nullptr &&
        xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        validation_report_ = FactoryValidationReport{};
        xSemaphoreGive(status_mutex_);
    }
}

bool FactoryRunner::start_burn_in_job(
    JobManager* job_mgr,
    uint32_t duration_minutes,
    uint8_t brightness,
    uint32_t* out_job_id
) {
    if (job_mgr == nullptr) return false;
    if (burn_in_active_) return false;
    // LG-038.3 §18: Burn-In is explicit physical use of the worker.
    if (!ensure_worker_started()) return false;

    uint32_t job_id = 0;
    esp_err_t err = job_mgr->start_job(JobType::BurnIn, &job_id);
    if (err != ESP_OK) return false;

    if (out_job_id) *out_job_id = job_id;

    burn_in_job_mgr_ = job_mgr;
    burn_in_job_id_ = job_id;
    burn_in_started_ms_ = now_ms();
    burn_in_duration_ms_ = (duration_minutes == 0 ? 60 : duration_minutes) * 60 * 1000;
    burn_in_brightness_ = clamp_brightness(brightness, true);
    burn_in_active_ = true;

    uint32_t session_id = 0;
    return start_test_session(
        FactoryTestKind::BurnIn,
        0xFF,
        burn_in_duration_ms_,
        burn_in_brightness_,
        &session_id
    );
}

bool FactoryRunner::stop_burn_in_job() {
    if (!burn_in_active_) return false;
    if (burn_in_job_mgr_ && burn_in_job_id_ != 0) {
        burn_in_job_mgr_->cancel_job(burn_in_job_id_);
    }
    burn_in_active_ = false;
    abort();
    return true;
}

void FactoryRunner::task_entry(void* argument) {
    static_cast<FactoryRunner*>(argument)->task();
}

void FactoryRunner::blackout() {
    source_manager_.release(ControlSource::Factory);
    controller_.blackout();
    xSemaphoreTake(status_mutex_, portMAX_DELAY);
    status_.active_effect = FactoryEffect::SafeBlackout;
    status_.outputs_enabled = false;
    status_.test_active = false;
    status_.effect_duration_ms = 0;
    status_.active_channel = 0xFF;
    xSemaphoreGive(status_mutex_);
    if (status_callback_) status_callback_();
}

void FactoryRunner::start_effect(FactoryEffect effect,
                                 uint32_t duration_ms,
                                 uint8_t brightness,
                                 uint32_t started_ms) {
    source_manager_.acquire(ControlSource::Factory);
    xSemaphoreTake(status_mutex_, portMAX_DELAY);
    status_.active_effect = effect;
    status_.effect_started_ms = started_ms;
    status_.effect_duration_ms = clamp_duration(duration_ms, effect == FactoryEffect::StressCycle);
    status_.brightness = clamp_brightness(brightness, effect == FactoryEffect::StressCycle);
    status_.outputs_enabled = true;
    status_.test_active = effect != FactoryEffect::SafeBlackout;
    xSemaphoreGive(status_mutex_);
    if (status_callback_) status_callback_();
}

void FactoryRunner::set_stream_frame(const StreamFramePayload& payload,
                                     uint8_t brightness,
                                     uint32_t duration_ms) {
    const uint32_t current_ms = now_ms();
    source_manager_.acquire(ControlSource::Factory);
    xSemaphoreTake(status_mutex_, portMAX_DELAY);
    stream_frame_.stream = payload;
    stream_frame_.brightness = brightness;
    stream_active_ = true;
    stream_last_ms_ = current_ms;
    direct_test_active_ = false;
    status_.auto_loop = false;
    status_.active_effect = FactoryEffect::SafeBlackout;
    status_.effect_started_ms = current_ms;
    status_.effect_duration_ms = clamp_duration(duration_ms);
    status_.brightness = clamp_brightness(brightness);
    status_.outputs_enabled = true;
    status_.test_active = true;
    xSemaphoreGive(status_mutex_);
    if (status_callback_) status_callback_();
}

void FactoryRunner::apply_command(const FactoryCommand& command,
                                  uint32_t current_ms) {
    if (command.command_id != 0) {
        if (pending_command_id_ != 0 && pending_command_id_ != command.command_id && journal_) {
            journal_->superseded(pending_command_id_);
        }
        pending_command_id_ = command.command_id;
        if (journal_) {
            journal_->applying(pending_command_id_);
        }
    }

    switch (command.type) {
        case FactoryCommandType::Abort:
            direct_test_active_ = false;
            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            stream_active_ = false;
            status_.auto_loop = false;
            xSemaphoreGive(status_mutex_);
            blackout();
            if (pending_command_id_ != 0 && journal_) {
                journal_->applied(pending_command_id_);
                pending_command_id_ = 0;
            }
            return;
        case FactoryCommandType::SetAutoLoop:
            direct_test_active_ = false;
            if (command.enabled) {
                source_manager_.acquire(ControlSource::Factory);
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                stream_active_ = false;
                status_.auto_loop = true;
                status_.effect_started_ms = current_ms;
                status_.effect_duration_ms = kLoopEffectDurationMs;
                xSemaphoreGive(status_mutex_);
                start_effect(kLoopEffects[0], kLoopEffectDurationMs,
                             LG_FACTORY_DEFAULT_BRIGHTNESS, current_ms);
            } else {
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                status_.auto_loop = false;
                xSemaphoreGive(status_mutex_);
                blackout();
                if (pending_command_id_ != 0 && journal_) {
                    journal_->applied(pending_command_id_);
                    pending_command_id_ = 0;
                }
            }
            if (status_callback_) status_callback_();
            return;
        case FactoryCommandType::SelectEffect:
            direct_test_active_ = false;
            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            stream_active_ = false;
            status_.auto_loop = false;
            xSemaphoreGive(status_mutex_);
            if (status_callback_) status_callback_();
            start_effect(command.effect, command.duration_ms, command.brightness, current_ms);
            return;
        case FactoryCommandType::PixelLaneSolid:
        case FactoryCommandType::PwmChannelLevel:
        case FactoryCommandType::ChannelTest:
            source_manager_.acquire(ControlSource::Factory);
            direct_test_ = command;
            direct_test_active_ = true;
            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            stream_active_ = false;
            status_.auto_loop = false;
            status_.active_effect = FactoryEffect::SafeBlackout;
            status_.effect_started_ms = current_ms;
            status_.effect_duration_ms = clamp_duration(command.duration_ms);
            status_.brightness = clamp_brightness(command.brightness);
            status_.active_channel = command.lane_or_channel;
            status_.outputs_enabled = true;
            status_.test_active = true;
            xSemaphoreGive(status_mutex_);
            if (status_callback_) status_callback_();
            return;
        case FactoryCommandType::StreamFrame:
            set_stream_frame(command.stream, command.brightness, command.duration_ms);
            return;
        case FactoryCommandType::StartSession: {
            direct_test_active_ = false;
            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            stream_active_ = false;
            status_.auto_loop = false;

            const bool is_burn_in = (command.session_kind == FactoryTestKind::BurnIn);
            uint32_t clamped_dur = clamp_duration(command.duration_ms, is_burn_in);
            const uint8_t clamped_b = clamp_brightness(command.brightness, is_burn_in);

            status_.session.session_id = command.session_id;
            status_.session.kind = command.session_kind;
            status_.session.state = FactoryTestState::Running;
            status_.session.channel = command.lane_or_channel;
            status_.session.started_ms = current_ms;
            status_.session.finished_ms = 0;
            status_.session.command_id = command.command_id;
            status_.session.operator_confirmation_required = true;
            status_.session.error_code = 0;
            status_.session.notes[0] = '\0';
            status_.session.owner = command.session_owner;
            strncpy(status_.session.certification_test_id, command.session_cert_test_id, sizeof(status_.session.certification_test_id) - 1);
            status_.session.certification_test_id[sizeof(status_.session.certification_test_id) - 1] = '\0';

            status_.active_channel = command.lane_or_channel;
            status_.effect_started_ms = current_ms;
            status_.effect_duration_ms = clamped_dur;
            status_.brightness = clamped_b;
            status_.outputs_enabled = true;
            status_.test_active = true;

            if (command.session_kind == FactoryTestKind::AddressableChannel ||
                command.session_kind == FactoryTestKind::PwmChannel) {
                direct_test_.type = FactoryCommandType::ChannelTest;
                direct_test_.lane_or_channel = command.lane_or_channel;
                const uint32_t req_dur = (command.session_kind == FactoryTestKind::AddressableChannel) ? 4000 : 3000;
                if (clamped_dur < req_dur) {
                    clamped_dur = req_dur;
                    status_.effect_duration_ms = clamped_dur;
                }
                direct_test_.duration_ms = clamped_dur;
                direct_test_.brightness = clamped_b;
                direct_test_active_ = true;
                status_.active_effect = FactoryEffect::SafeBlackout;
                status_.session.stage_count = 6;
                status_.session.stage_index = 1;
                status_.session.stage = (command.session_kind == FactoryTestKind::AddressableChannel)
                                            ? FactoryTestStage::Red
                                            : FactoryTestStage::Pwm0;
            } else if (command.session_kind == FactoryTestKind::CombinedOutputs) {
                status_.active_effect = FactoryEffect::CombinedPulse;
            } else if (command.session_kind == FactoryTestKind::SafetyGate) {
                status_.active_effect = FactoryEffect::WhiteWalk;
            } else if (command.session_kind == FactoryTestKind::BurnIn) {
                status_.active_effect = FactoryEffect::StressCycle;
            }

            source_manager_.acquire(ControlSource::Factory);
            xSemaphoreGive(status_mutex_);

            if (pending_command_id_ != 0 && journal_) {
                journal_->applied(pending_command_id_);
                pending_command_id_ = 0;
            }
            if (status_callback_) status_callback_();
            return;
        }
        case FactoryCommandType::ConfirmSession: {
            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            if (status_.session.session_id == command.session_id) {
                status_.session.finished_ms = current_ms;
                status_.session.state = command.session_passed ? FactoryTestState::Passed : FactoryTestState::Failed;
                strncpy(status_.session.notes, command.session_notes, sizeof(status_.session.notes) - 1);
                status_.session.notes[sizeof(status_.session.notes) - 1] = '\0';

                // Record into validation report
                const uint8_t ch = status_.session.channel;
                if (status_.session.kind == FactoryTestKind::AddressableChannel && ch < 8) {
                    validation_report_.addressable[ch] = { .tested = true, .passed = command.session_passed, .timestamp_ms = current_ms };
                } else if (status_.session.kind == FactoryTestKind::PwmChannel && ch >= 8 && ch < 16) {
                    validation_report_.pwm[ch - 8] = { .tested = true, .passed = command.session_passed, .timestamp_ms = current_ms };
                } else if (status_.session.kind == FactoryTestKind::CombinedOutputs) {
                    validation_report_.combined = { .tested = true, .passed = command.session_passed, .timestamp_ms = current_ms };
                } else if (status_.session.kind == FactoryTestKind::SafetyGate) {
                    validation_report_.safety_gate = { .tested = true, .passed = command.session_passed, .timestamp_ms = current_ms };
                } else if (status_.session.kind == FactoryTestKind::BurnIn) {
                    validation_report_.burn_in = { .tested = true, .passed = command.session_passed, .timestamp_ms = current_ms };
                    validation_report_.burn_in_duration_s = (current_ms - status_.session.started_ms) / 1000;
                }
            }
            xSemaphoreGive(status_mutex_);

            blackout();

            if (pending_command_id_ != 0 && journal_) {
                journal_->applied(pending_command_id_);
                pending_command_id_ = 0;
            }
            if (status_callback_) status_callback_();
            return;
        }
    }
}

void FactoryRunner::task() {
    uint8_t loop_index = 0;
    uint32_t loop_started_ms = now_ms();
    uint32_t blackout_started_ms = 0;
    bool between_loop_effects = false;
    blackout();
    if (status().auto_loop) {
        source_manager_.acquire(ControlSource::Factory);
        start_effect(kLoopEffects[loop_index], kLoopEffectDurationMs,
                     LG_FACTORY_DEFAULT_BRIGHTNESS, now_ms());
        loop_started_ms = now_ms();
    }
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (!shutting_down_.load(std::memory_order_acquire)) {
        const uint32_t current_ms = now_ms();
        FactoryCommand command;
        while (xQueueReceive(queue_, &command, 0) == pdPASS) {
            apply_command(command, current_ms);
            loop_started_ms = current_ms;
        }

        // Periodic Burn-In Job progress update
        if (burn_in_active_ && burn_in_job_mgr_ && (current_ms % 1000 < kFramePeriodMs)) {
            const uint32_t elapsed = current_ms - burn_in_started_ms_;
            const uint8_t prog = burn_in_duration_ms_ > 0 ? std::min<uint8_t>(100, (elapsed * 100ULL) / burn_in_duration_ms_) : 0;
            burn_in_job_mgr_->update_progress(burn_in_job_id_, JobState::Running, prog);
        }

        bool is_streaming = false;
        StreamFramePayload local_stream{};
        uint32_t local_stream_last_ms = 0;
        uint8_t local_stream_brightness = 0;
        xSemaphoreTake(status_mutex_, portMAX_DELAY);
        if (stream_active_) {
            is_streaming = true;
            local_stream = stream_frame_.stream;
            local_stream_last_ms = stream_last_ms_;
            local_stream_brightness = stream_frame_.brightness;
        }
        xSemaphoreGive(status_mutex_);
        if (is_streaming) {
            source_manager_.acquire(ControlSource::Factory, 1000);
            const uint8_t b = local_stream_brightness;
            controller_.lock();
            for (uint8_t lane = 0; lane < 8; ++lane) {
                controller_.pixels().fill(lane,
                    static_cast<uint8_t>((uint16_t(local_stream.lane_rgb[lane][0]) * b) / 255),
                    static_cast<uint8_t>((uint16_t(local_stream.lane_rgb[lane][1]) * b) / 255),
                    static_cast<uint8_t>((uint16_t(local_stream.lane_rgb[lane][2]) * b) / 255));
                const uint16_t duty = static_cast<uint16_t>(
                    (uint32_t(local_stream.lane_rgb[lane][0] + local_stream.lane_rgb[lane][1] + local_stream.lane_rgb[lane][2]) * 4095) / (255 * 3));
                controller_.pwm().set(lane, duty);
            }
            esp_err_t commit_err = controller_.try_commit_locked();
            controller_.unlock();
            if (commit_err != ESP_OK) {
                if (pending_command_id_ != 0 && journal_) {
                    journal_->failed(pending_command_id_, commit_err);
                    pending_command_id_ = 0;
                }
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                ++status_.commit_failures;
                xSemaphoreGive(status_mutex_);
                blackout();
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }
            if (pending_command_id_ != 0 && journal_) {
                journal_->applied(pending_command_id_);
                pending_command_id_ = 0;
            }
            if (current_ms - local_stream_last_ms > 1000) {
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                stream_active_ = false;
                xSemaphoreGive(status_mutex_);
                blackout();
            }
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
            continue;
        }
        const FactoryStatus current = status();
        if (direct_test_active_) {
            const uint32_t elapsed = current_ms - current.effect_started_ms;
            controller_.lock();
            controller_.pixels().clear();
            controller_.pwm().set_all(0);
            if (direct_test_.type == FactoryCommandType::PixelLaneSolid) {
                const uint8_t b = current.brightness;
                controller_.pixels().fill(direct_test_.lane_or_channel,
                    static_cast<uint8_t>((uint16_t(direct_test_.red) * b) / 255),
                    static_cast<uint8_t>((uint16_t(direct_test_.green) * b) / 255),
                    static_cast<uint8_t>((uint16_t(direct_test_.blue) * b) / 255));
            }
            if (direct_test_.type == FactoryCommandType::PwmChannelLevel) {
                controller_.pwm().set(direct_test_.lane_or_channel, direct_test_.pwm_duty);
            }
            if (direct_test_.type == FactoryCommandType::ChannelTest) {
                const uint8_t ch = direct_test_.lane_or_channel;
                const ChannelHardwareDescriptor hw = channel_hardware(ch);
                if (hw.kind == ChannelKind::Addressable) {
                    uint8_t stage_idx = 1;
                    FactoryTestStage st = FactoryTestStage::Red;
                    if (elapsed < 500) {
                        stage_idx = 1;
                        st = FactoryTestStage::Red;
                        controller_.pixels().fill(hw.hardware_id, 255, 0, 0);
                    } else if (elapsed < 1000) {
                        stage_idx = 2;
                        st = FactoryTestStage::Green;
                        controller_.pixels().fill(hw.hardware_id, 0, 255, 0);
                    } else if (elapsed < 1500) {
                        stage_idx = 3;
                        st = FactoryTestStage::Blue;
                        controller_.pixels().fill(hw.hardware_id, 0, 0, 255);
                    } else if (elapsed < 2000) {
                        stage_idx = 4;
                        st = FactoryTestStage::White;
                        controller_.pixels().fill(hw.hardware_id, 255, 255, 255, 255);
                    } else if (elapsed < 3500) {
                        stage_idx = 5;
                        st = FactoryTestStage::Chase;
                        controller_.pixels().clear();
                        const uint16_t lane_px = controller_.pixels().configured_pixels(hw.hardware_id);
                        if (lane_px > 0) {
                            const uint32_t chase_elapsed = elapsed - 2000;
                            const uint16_t chase_px = (chase_elapsed / 50) % lane_px;
                            controller_.pixels().set(hw.hardware_id, chase_px, 255, 255, 255, 255);
                        }
                    } else {
                        stage_idx = 6;
                        st = FactoryTestStage::Blackout;
                        controller_.pixels().clear();
                    }
                    xSemaphoreTake(status_mutex_, portMAX_DELAY);
                    status_.session.stage_index = stage_idx;
                    status_.session.stage_count = 6;
                    status_.session.stage = st;
                    xSemaphoreGive(status_mutex_);
                } else {
                    uint8_t stage_idx = 1;
                    FactoryTestStage st = FactoryTestStage::Pwm0;
                    uint16_t duty = 0;
                    if (elapsed < 500) {
                        stage_idx = 1;
                        st = FactoryTestStage::Pwm0;
                        duty = 0;
                    } else if (elapsed < 1000) {
                        stage_idx = 2;
                        st = FactoryTestStage::Pwm25;
                        duty = 1024;
                    } else if (elapsed < 1500) {
                        stage_idx = 3;
                        st = FactoryTestStage::Pwm50;
                        duty = 2048;
                    } else if (elapsed < 2000) {
                        stage_idx = 4;
                        st = FactoryTestStage::Pwm75;
                        duty = 3072;
                    } else if (elapsed < 2500) {
                        stage_idx = 5;
                        st = FactoryTestStage::Pwm100;
                        duty = 4095;
                    } else {
                        stage_idx = 6;
                        st = FactoryTestStage::Pwm0;
                        duty = 0;
                    }
                    controller_.pwm().set(hw.hardware_id, duty);
                    xSemaphoreTake(status_mutex_, portMAX_DELAY);
                    status_.session.stage_index = stage_idx;
                    status_.session.stage_count = 6;
                    status_.session.stage = st;
                    xSemaphoreGive(status_mutex_);
                }
            }
            esp_err_t commit_err = controller_.try_commit_locked();
            controller_.unlock();
            if (commit_err != ESP_OK) {
                if (pending_command_id_ != 0 && journal_) {
                    journal_->failed(pending_command_id_, commit_err);
                    pending_command_id_ = 0;
                }
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                ++status_.commit_failures;
                if (status_.session.state == FactoryTestState::Running) {
                    status_.session.state = FactoryTestState::Failed;
                    status_.session.error_code = commit_err;
                    status_.session.finished_ms = current_ms;
                }
                xSemaphoreGive(status_mutex_);
                blackout();
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }
            if (pending_command_id_ != 0 && journal_) {
                journal_->applied(pending_command_id_);
                pending_command_id_ = 0;
            }
            if (elapsed >= current.effect_duration_ms) {
                direct_test_active_ = false;
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                if (status_.session.state == FactoryTestState::Running) {
                    // Software must NOT claim physical hardware passed without operator confirmation!
                    if (status_.session.operator_confirmation_required) {
                        status_.session.state = FactoryTestState::WaitingForOperator;
                    } else {
                        status_.session.state = FactoryTestState::Passed;
                    }
                    status_.session.finished_ms = current_ms;
                }
                xSemaphoreGive(status_mutex_);
                blackout();
            }
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
            continue;
        }
        if (current.auto_loop) {
            if (between_loop_effects) {
                if (current_ms - blackout_started_ms >= kInterEffectBlackoutMs) {
                    between_loop_effects = false;
                    loop_index = (loop_index + 1) % (sizeof(kLoopEffects) / sizeof(kLoopEffects[0]));
                    start_effect(kLoopEffects[loop_index], kLoopEffectDurationMs,
                                 LG_FACTORY_DEFAULT_BRIGHTNESS, current_ms);
                    loop_started_ms = current_ms;
                }
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }
            if (current_ms - loop_started_ms >= kLoopEffectDurationMs) {
                source_manager_.release(ControlSource::Factory);
                between_loop_effects = true;
                blackout_started_ms = current_ms;
                if (status_callback_) status_callback_();
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }
        } else if (current.test_active && current_ms - current.effect_started_ms >= current.effect_duration_ms) {
            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            if (status_.session.state == FactoryTestState::Running) {
                if (status_.session.operator_confirmation_required) {
                    status_.session.state = FactoryTestState::WaitingForOperator;
                } else {
                    status_.session.state = FactoryTestState::Passed;
                }
                status_.session.finished_ms = current_ms;
            }
            xSemaphoreGive(status_mutex_);

            if (burn_in_active_) {
                if (burn_in_job_mgr_) {
                    burn_in_job_mgr_->complete_job(burn_in_job_id_);
                }
                burn_in_active_ = false;
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                validation_report_.burn_in = { .tested = true, .passed = true, .timestamp_ms = current_ms };
                validation_report_.burn_in_duration_s = (current_ms - burn_in_started_ms_) / 1000;
                xSemaphoreGive(status_mutex_);
            }

            blackout();
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
            continue;
        }
        if (current.test_active) {
            const uint32_t elapsed = current_ms - current.effect_started_ms;
            controller_.lock();
            render_effect(current.active_effect, controller_, elapsed, current.brightness);
            esp_err_t commit_err = controller_.try_commit_locked();
            controller_.unlock();
            if (commit_err != ESP_OK) {
                if (pending_command_id_ != 0 && journal_) {
                    journal_->failed(pending_command_id_, commit_err);
                    pending_command_id_ = 0;
                }
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                ++status_.commit_failures;
                if (status_.session.state == FactoryTestState::Running) {
                    status_.session.state = FactoryTestState::Failed;
                    status_.session.error_code = commit_err;
                    status_.session.finished_ms = current_ms;
                    if (status_.session.kind == FactoryTestKind::BurnIn) {
                        ++validation_report_.burn_in_errors;
                    }
                }
                xSemaphoreGive(status_mutex_);

                if (burn_in_active_) {
                    if (burn_in_job_mgr_) {
                        burn_in_job_mgr_->fail_job(burn_in_job_id_, commit_err, "Commit error");
                    }
                    burn_in_active_ = false;
                }

                blackout();
                vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }
            if (pending_command_id_ != 0 && journal_) {
                journal_->applied(pending_command_id_);
                pending_command_id_ = 0;
            }
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
    }
    blackout();
    if (shutdown_sem_) xSemaphoreGive(shutdown_sem_);
    vTaskDelete(NULL);
}

} // namespace lg

