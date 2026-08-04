// components/signage/SignageEngine.cpp
#include "SignageEngine.hpp"
#include "SignageEffects.hpp"
#include "GammaLut.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char* TAG = "signage";

namespace lg {
namespace {

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return static_cast<uint8_t>(a + (int16_t(b) - a) * t / 255);
}

uint16_t lerp16(uint16_t a, uint16_t b, uint8_t t) {
    return static_cast<uint16_t>(a + (int32_t(b) - a) * t / 255);
}

BrandPreset blend_presets(const BrandPreset& from, const BrandPreset& to, uint8_t t) {
    BrandPreset result = to; // Copy target's structure/metadata
    result.brightness = lerp8(from.brightness, to.brightness, t);

    for (uint8_t i = 0; i < 8; ++i) {
        result.channels[i].pixel_color.r = lerp8(
            from.channels[i].pixel_color.r, to.channels[i].pixel_color.r, t);
        result.channels[i].pixel_color.g = lerp8(
            from.channels[i].pixel_color.g, to.channels[i].pixel_color.g, t);
        result.channels[i].pixel_color.b = lerp8(
            from.channels[i].pixel_color.b, to.channels[i].pixel_color.b, t);
        result.channels[i].pwm_duty = lerp16(
            from.channels[i].pwm_duty, to.channels[i].pwm_duty, t);
    }

    return result;
}

} // namespace

SignageEngine::SignageEngine(OutputArbitrator& output, ConfigStore& config)
    : output_(output), config_(config) {}

esp_err_t SignageEngine::start() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;

    // Load device config for watchdog and power settings
    DeviceConfig dev_cfg;
    config_.load_config(dev_cfg);

    watchdog_timeout_s_ = dev_cfg.watchdog_timeout_s;
    watchdog_enabled_ = (watchdog_timeout_s_ > 0);

    power_budget_.set_limit_mw(dev_cfg.max_power_mw);
    power_budget_.set_soft_start(dev_cfg.soft_start_enabled, dev_cfg.soft_start_ms);

    // Load last active preset (power-loss recovery)
    uint8_t active_idx = 0;
    config_.load_active_state(active_idx);

    BrandPreset preset;
    if (config_.load_preset(active_idx, preset) != ESP_OK) {
        // Fallback to preset 0
        active_idx = 0;
        if (config_.load_preset(0, preset) != ESP_OK) {
            ESP_LOGW(TAG, "No presets available, starting with blackout");
            preset = BrandPreset{};
            preset.set_name("Default");
        }
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    current_preset_ = preset;
    active_index_ = active_idx;
    enable_ms_ = now_ms();
    preset_started_ms_ = enable_ms_;
    last_command_ms_ = enable_ms_;
    xSemaphoreGive(mutex_);

    ESP_LOGI(TAG, "Starting with preset %u: \"%s\"", active_idx, preset.name);

    output_.acquire(OutputPriority::Signage); // Acquire indefinite lease

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry, "signage", 4096, this, 4, &task_handle_, 1
    );

    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void SignageEngine::task_entry(void* arg) {
    static_cast<SignageEngine*>(arg)->task();
    vTaskDelete(nullptr);
}

void SignageEngine::task() {
    power_budget_.on_enable();

    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        const uint32_t current_ms = now_ms();
        render_frame(current_ms);
        check_watchdog(current_ms);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kFramePeriodMs));
    }
}

void SignageEngine::render_frame(uint32_t current_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);

    BrandPreset render_preset;
    uint16_t beat_period_ms = 500;

    if (transitioning_) {
        const uint32_t elapsed = current_ms - transition_start_ms_;
        if (elapsed >= transition_duration_ms_) {
            // Transition complete
            current_preset_ = target_preset_;
            transitioning_ = false;
            preset_started_ms_ = current_ms;
            render_preset = current_preset_;
            ESP_LOGI(TAG, "Transition complete → \"%s\"", current_preset_.name);
        } else {
            // Mid-transition: blend
            const uint8_t t = static_cast<uint8_t>(
                (uint32_t(elapsed) * 255) / transition_duration_ms_
            );
            render_preset = blend_presets(current_preset_, target_preset_, t);
        }
    } else {
        render_preset = current_preset_;
    }

    const uint32_t effect_elapsed = current_ms - preset_started_ms_;
    beat_period_ms = beat_period_ms_;
    xSemaphoreGive(mutex_);

    // Apply soft-start factor
    if (first_frame_) {
        first_frame_ = false;
    }
    const uint32_t ms_since_enable = current_ms - enable_ms_;
    const uint8_t ss_factor = power_budget_.soft_start_factor(ms_since_enable);
    if (ss_factor < 255) {
        render_preset.brightness = static_cast<uint8_t>(
            (uint16_t(render_preset.brightness) * ss_factor) / 255
        );
    }

    // Apply power budget
    power_budget_.enforce(output_.pixels(), output_.pwm(), render_preset.brightness);

    // Render the effects per lane
    if (output_.begin_frame(OutputPriority::Signage)) {
        output_.pixels().clear();
        output_.pwm().set_all(0);
        if (preset_is_visible(render_preset, effect_elapsed, beat_period_ms)) {
            for (uint8_t i = 0; i < 8; ++i) {
                render_lane_effect(i, output_.controller(), render_preset.channels[i], render_preset.brightness, effect_elapsed);
            }
        }
        output_.commit_frame(OutputPriority::Signage);
    }
}

void SignageEngine::check_watchdog(uint32_t current_ms) {
    if (!watchdog_enabled_ || fallback_active_) return;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t elapsed_s = (current_ms - last_command_ms_) / 1000;
    xSemaphoreGive(mutex_);

    if (elapsed_s > watchdog_timeout_s_) {
        DeviceConfig cfg;
        config_.load_config(cfg);
        
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const uint8_t current_idx = active_index_;
        xSemaphoreGive(mutex_);

        if (current_idx == cfg.fallback_preset_index) {
            // Already on fallback, don't trigger watchdog. Reset timer.
            xSemaphoreTake(mutex_, portMAX_DELAY);
            last_command_ms_ = current_ms;
            xSemaphoreGive(mutex_);
            return;
        }

        ESP_LOGW(TAG, "Watchdog: no commands for %lu s, activating fallback %u", (unsigned long)elapsed_s, cfg.fallback_preset_index);

        BrandPreset fallback;
        if (config_.load_preset(cfg.fallback_preset_index, fallback) == ESP_OK) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            target_preset_ = fallback;
            transition_start_ms_ = current_ms;
            transition_duration_ms_ = std::max<uint32_t>(1, fallback.transition_ms);
            transitioning_ = true;
            fallback_active_ = true;
            active_index_ = cfg.fallback_preset_index;
            xSemaphoreGive(mutex_);
        } else {
            ESP_LOGE(TAG, "Watchdog fallback preset %u not found", cfg.fallback_preset_index);
            xSemaphoreTake(mutex_, portMAX_DELAY);
            fallback_active_ = true; // Prevent retry loop
            xSemaphoreGive(mutex_);
        }
    }
}

void SignageEngine::activate_preset(uint8_t index) {
    BrandPreset preset;
    if (config_.load_preset(index, preset) != ESP_OK) {
        ESP_LOGW(TAG, "Preset %u not found", index);
        return;
    }

    const uint32_t current_ms = now_ms();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (transitioning_) {
        // If already transitioning, snap current to wherever we are
        const uint32_t elapsed = current_ms - transition_start_ms_;
        const uint8_t t = std::min(uint8_t(255),
            static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
        current_preset_ = blend_presets(current_preset_, target_preset_, t);
    }

    target_preset_ = preset;
    transition_start_ms_ = current_ms;
    transition_duration_ms_ = std::max<uint32_t>(1, preset.transition_ms);
    transitioning_ = true;
    active_index_ = index;
    fallback_active_ = false;
    xSemaphoreGive(mutex_);

    // Persist active state for power-loss recovery
    config_.save_active_state(index);

    ESP_LOGI(TAG, "Transitioning to preset %u: \"%s\" (%u ms fade)",
             index, preset.name, preset.transition_ms);
}

void SignageEngine::activate_preset_direct(const BrandPreset& preset, bool immediate) {
    const uint32_t current_ms = now_ms();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (immediate) {
        current_preset_ = preset;
        transitioning_ = false;
        preset_started_ms_ = current_ms;
    } else {
        target_preset_ = preset;
        transition_start_ms_ = current_ms;
        transition_duration_ms_ = std::max<uint32_t>(1, preset.transition_ms);
        transitioning_ = true;
    }
    fallback_active_ = false;
    xSemaphoreGive(mutex_);
}

void SignageEngine::feed_watchdog() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    last_command_ms_ = now_ms();
    if (fallback_active_) {
        fallback_active_ = false;
        ESP_LOGI(TAG, "Watchdog fed, exiting fallback mode");
    }
    xSemaphoreGive(mutex_);
}

uint8_t SignageEngine::active_preset_index() const {
    return active_index_;
}

void SignageEngine::set_beat_period(uint16_t period_ms) {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    beat_period_ms_ = std::max<uint16_t>(
        10,
        std::min<uint16_t>(period_ms, 60000)
    );
    xSemaphoreGive(mutex_);
}

} // namespace lg
