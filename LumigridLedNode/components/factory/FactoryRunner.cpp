// components/factory/FactoryRunner.cpp
#include "FactoryRunner.hpp"
#include "FactoryApp.hpp"

#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

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

uint8_t clamp_brightness(uint8_t brightness) {
    // Intentionally capped for early board bring-up.
    return std::min<uint8_t>(brightness, 96);
}

uint32_t clamp_duration(uint32_t duration_ms) {
    if (duration_ms == 0) {
        return LG_FACTORY_MAX_TEST_MS;
    }

    return std::min(duration_ms, LG_FACTORY_MAX_TEST_MS);
}

} // namespace

FactoryRunner::FactoryRunner(OutputController& output) : output_(output) {}

esp_err_t FactoryRunner::start() {
    queue_ = xQueueCreate(kQueueDepth, sizeof(FactoryCommand));
    if (queue_ == nullptr) return ESP_ERR_NO_MEM;

    status_mutex_ = xSemaphoreCreateMutex();
    if (status_mutex_ == nullptr) return ESP_ERR_NO_MEM;

    status_.active_effect = FactoryEffect::SafeBlackout;
    status_.brightness = LG_FACTORY_DEFAULT_BRIGHTNESS;
    status_.auto_loop = true;
    status_.outputs_enabled = false;

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry,
        "lg_factory",
        6144,
        this,
        5,
        &task_handle_,
        1
    );

    return created == pdPASS ? ESP_OK : ESP_FAIL;
}

bool FactoryRunner::enqueue(const FactoryCommand& command) {
    if (queue_ == nullptr) return false;

    if (xQueueSend(queue_, &command, 0) == pdPASS) {
        return true;
    }

    if (status_mutex_ != nullptr &&
        xSemaphoreTake(status_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        ++status_.command_rejections;
        xSemaphoreGive(status_mutex_);
    }

    return false;
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

void FactoryRunner::task_entry(void* argument) {
    static_cast<FactoryRunner*>(argument)->task();
}

void FactoryRunner::blackout() {
    output_.blackout();

    xSemaphoreTake(status_mutex_, portMAX_DELAY);
    status_.active_effect = FactoryEffect::SafeBlackout;
    status_.outputs_enabled = false;
    status_.test_active = false;
    status_.effect_duration_ms = 0;
    xSemaphoreGive(status_mutex_);

    if (status_callback_) status_callback_();
}

void FactoryRunner::start_effect(FactoryEffect effect,
                                 uint32_t duration_ms,
                                 uint8_t brightness,
                                 uint32_t started_ms) {
    output_.blackout();
    output_.enable();

    xSemaphoreTake(status_mutex_, portMAX_DELAY);
    status_.active_effect = effect;
    status_.effect_started_ms = started_ms;
    status_.effect_duration_ms = clamp_duration(duration_ms);
    status_.brightness = clamp_brightness(brightness);
    status_.outputs_enabled = true;
    status_.test_active = effect != FactoryEffect::SafeBlackout;
    xSemaphoreGive(status_mutex_);

    if (status_callback_) status_callback_();
}

void FactoryRunner::set_stream_frame(const StreamFramePayload& payload,
                                     uint8_t brightness,
                                     uint32_t duration_ms) {
    const uint32_t current_ms = now_ms();

    xSemaphoreTake(status_mutex_, portMAX_DELAY);
    // Only set flags here — output_.enable() happens in the task loop
    // to keep all OutputController operations on a single task.
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
    switch (command.type) {
        case FactoryCommandType::Abort:
            direct_test_active_ = false;
            blackout();
            return;

        case FactoryCommandType::SetAutoLoop:
            output_.blackout();

            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            status_.auto_loop = command.enabled;
            status_.effect_started_ms = current_ms;
            status_.effect_duration_ms = kLoopEffectDurationMs;
            xSemaphoreGive(status_mutex_);

            if (status_callback_) status_callback_();
            return;

        case FactoryCommandType::SelectEffect:
            direct_test_active_ = false;

            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            status_.auto_loop = false;
            xSemaphoreGive(status_mutex_);

            if (status_callback_) status_callback_();

            start_effect(
                command.effect,
                command.duration_ms,
                command.brightness,
                current_ms
            );
            return;

        case FactoryCommandType::PixelLaneSolid:
        case FactoryCommandType::PwmChannelLevel:
            output_.blackout();
            output_.enable();

            direct_test_ = command;
            direct_test_active_ = true;

            xSemaphoreTake(status_mutex_, portMAX_DELAY);
            status_.auto_loop = false;
            status_.active_effect = FactoryEffect::SafeBlackout;
            status_.effect_started_ms = current_ms;
            status_.effect_duration_ms = clamp_duration(command.duration_ms);
            status_.brightness = clamp_brightness(command.brightness);
            status_.outputs_enabled = true;
            status_.test_active = true;
            xSemaphoreGive(status_mutex_);

            if (status_callback_) status_callback_();
            return;
        case FactoryCommandType::StreamFrame:
            set_stream_frame(command.stream, command.brightness, command.duration_ms);
            return;
    }
}

void FactoryRunner::task() {
    uint8_t loop_index = 0;
    uint32_t loop_started_ms = now_ms();
    uint32_t blackout_started_ms = 0;
    bool between_loop_effects = false;

    blackout();

    // Kick off the first loop effect immediately at boot.
    if (status().auto_loop) {
        start_effect(
            kLoopEffects[loop_index],
            kLoopEffectDurationMs,
            LG_FACTORY_DEFAULT_BRIGHTNESS,
            now_ms()
        );
        loop_started_ms = now_ms();
    }

    for (;;) {
        const uint32_t current_ms = now_ms();

        FactoryCommand command;
        while (xQueueReceive(queue_, &command, 0) == pdPASS) {
            apply_command(command, current_ms);
            loop_started_ms = current_ms;
            between_loop_effects = false;
        }

        // --- Streaming path: read stream state under mutex ---
        bool is_streaming = false;
        bool stream_needs_enable = false;
        StreamFramePayload local_stream;
        uint32_t local_stream_last_ms = 0;
        uint8_t local_stream_brightness = 0;

        xSemaphoreTake(status_mutex_, portMAX_DELAY);
        if (stream_active_) {
            is_streaming = true;
            stream_needs_enable = !output_.enabled();
            local_stream = stream_frame_.stream;
            local_stream_last_ms = stream_last_ms_;
            local_stream_brightness = stream_frame_.brightness;
        }
        xSemaphoreGive(status_mutex_);

        if (is_streaming) {
            if (stream_needs_enable) {
                output_.enable();
            }

            const uint8_t b = local_stream_brightness;

            for (uint8_t lane = 0; lane < 8; ++lane) {
                output_.pixels().fill(
                    lane,
                    {
                        static_cast<uint8_t>((uint16_t(local_stream.lane_rgb[lane][0]) * b) / 255),
                        static_cast<uint8_t>((uint16_t(local_stream.lane_rgb[lane][1]) * b) / 255),
                        static_cast<uint8_t>((uint16_t(local_stream.lane_rgb[lane][2]) * b) / 255)
                    }
                );

                const uint16_t duty =
                    static_cast<uint16_t>(
                        (uint32_t(local_stream.lane_rgb[lane][0] +
                                  local_stream.lane_rgb[lane][1] +
                                  local_stream.lane_rgb[lane][2]) * 4095) / (255 * 3)
                    );

                output_.pwm().set(lane, duty);
            }

            output_.commit();

            if (current_ms - local_stream_last_ms > 1000) {
                xSemaphoreTake(status_mutex_, portMAX_DELAY);
                stream_active_ = false;
                xSemaphoreGive(status_mutex_);
                blackout();
            }

            vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
            continue;
        }

        // --- Non-streaming paths: snapshot status once per frame ---
        const FactoryStatus current = status();

        if (direct_test_active_) {
            const uint32_t elapsed = current_ms - current.effect_started_ms;

            output_.pixels().clear();
            output_.pwm().set_all(0);

            if (direct_test_.type == FactoryCommandType::PixelLaneSolid) {
                const uint8_t b = current.brightness;

                output_.pixels().fill(
                    direct_test_.lane_or_channel,
                    {
                        static_cast<uint8_t>((uint16_t(direct_test_.red) * b) / 255),
                        static_cast<uint8_t>((uint16_t(direct_test_.green) * b) / 255),
                        static_cast<uint8_t>((uint16_t(direct_test_.blue) * b) / 255)
                    }
                );
            }

            if (direct_test_.type == FactoryCommandType::PwmChannelLevel) {
                output_.pwm().set(
                    direct_test_.lane_or_channel,
                    direct_test_.pwm_duty
                );
            }

            output_.commit();

            if (elapsed >= current.effect_duration_ms) {
                direct_test_active_ = false;
                blackout();
            }

            vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
            continue;
        }

        if (current.auto_loop) {
            if (between_loop_effects) {
                if (current_ms - blackout_started_ms >= kInterEffectBlackoutMs) {
                    between_loop_effects = false;
                    loop_index = (loop_index + 1) %
                        (sizeof(kLoopEffects) / sizeof(kLoopEffects[0]));

                    start_effect(
                        kLoopEffects[loop_index],
                        kLoopEffectDurationMs,
                        LG_FACTORY_DEFAULT_BRIGHTNESS,
                        current_ms
                    );

                    loop_started_ms = current_ms;
                }

                vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }

            if (current_ms - loop_started_ms >= kLoopEffectDurationMs) {
                output_.blackout();
                between_loop_effects = true;
                blackout_started_ms = current_ms;

                if (status_callback_) status_callback_();
                vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
                continue;
            }
        } else if (current.test_active &&
                   current_ms - current.effect_started_ms >=
                   current.effect_duration_ms) {
            blackout();
            vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
            continue;
        }

        if (current.test_active) {
            const uint32_t elapsed = current_ms - current.effect_started_ms;

            render_effect(
                current.active_effect,
                output_,
                elapsed,
                current.brightness
            );

            output_.commit();
        }

        vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
    }
}

} // namespace lg
