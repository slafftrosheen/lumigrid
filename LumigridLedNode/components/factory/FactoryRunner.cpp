#include "FactoryRunner.hpp"
#include "../../main/FactoryApp.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lg {
namespace {
constexpr FactoryEffect kLoop[] = {
    FactoryEffect::Identity,
    FactoryEffect::PixelChase,
    FactoryEffect::Rainbow,
    FactoryEffect::WhiteWalk,
    FactoryEffect::PwmWalk,
    FactoryEffect::PwmFade,
    FactoryEffect::CombinedPulse
};

constexpr uint32_t kEffectDurationMs = 10000;
constexpr uint32_t kFramePeriodMs = 40; // 25 FPS; adequate for bench test
}

FactoryRunner::FactoryRunner(OutputController& output) : output_(output) {}

void FactoryRunner::start() {
    xTaskCreatePinnedToCore(
        task_entry,
        "factory_runner",
        6144,
        this,
        5,
        nullptr,
        1
    );
}

void FactoryRunner::select(FactoryEffect effect,
                           uint32_t duration_ms,
                           uint8_t brightness) {
    active_ = effect;
    duration_ms_ = std::min(duration_ms, LG_FACTORY_MAX_TEST_MS);
    brightness_ = brightness;
    started_ms_ = esp_timer_get_time() / 1000;
    auto_loop_ = false;
}

void FactoryRunner::stop() {
    auto_loop_ = false;
    active_ = FactoryEffect::SafeBlackout;
    output_.blackout();
}

void FactoryRunner::task_entry(void* argument) {
    static_cast<FactoryRunner*>(argument)->task();
}

void FactoryRunner::task() {
    uint8_t loop_index = 0;
    uint32_t effect_start = esp_timer_get_time() / 1000;

    output_.enable();

    for (;;) {
        const uint32_t now = esp_timer_get_time() / 1000;

        if (auto_loop_ && now - effect_start >= kEffectDurationMs) {
            output_.blackout();
            vTaskDelay(pdMS_TO_TICKS(300));

            loop_index = (loop_index + 1) % (sizeof(kLoop) / sizeof(kLoop[0]));
            active_ = kLoop[loop_index];
            effect_start = esp_timer_get_time() / 1000;
            output_.enable();
        }

        const uint32_t started = auto_loop_ ? effect_start : started_ms_;
        if (!auto_loop_ && duration_ms_ > 0 && now - started >= duration_ms_) {
            output_.blackout();
            active_ = FactoryEffect::SafeBlackout;
            duration_ms_ = 0;
        }

        render_effect(active_, output_, now - started, brightness_);
        output_.commit();

        vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
    }
}

} // namespace lg
