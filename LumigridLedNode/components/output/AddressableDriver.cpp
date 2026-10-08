// components/output/AddressableDriver.cpp
//
// Facade over ParallelRmtDriver (LG-038). Manages up to kLanes independent RMT lanes
// (WS2812 RGB / SK6812 RGBW) and a bounded diagnostic shadow of the staged values.
//
// Pixel ordering is per-fixture (PixelOrder): RGB, GRB, RGBW, GRBW. The wire buffer
// receives the configured byte order directly; SK6812 profile selects 4 bytes/pixel.
//
// RMT resolution: 10 MHz (100 ns per tick). Timing/encoder details live in
// ParallelRmtDriver.cpp. The managed led_strip component is not used.
#include "AddressableDriver.hpp"
#include "lg_board_pins.hpp"

#include "esp_log.h"
#include <cstdlib>
#include <cstring>

namespace lg {

static const char* TAG = "aled";

AddressableDriver::~AddressableDriver() {
    cancel_candidate();
    for (uint8_t lane = 0; lane < kLanes; ++lane) free_shadow(lane);
}

void AddressableDriver::free_shadow(uint8_t lane) {
    free(shadow_[lane]);
    shadow_[lane] = nullptr;
}

esp_err_t AddressableDriver::prepare_candidate(const DeviceConfig& cfg, uint32_t generic_reserve, uint32_t internal_reserve) {
    cancel_candidate();

    esp_err_t err = ESP_OK;
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        const auto& cc = cfg.channels[lane];
        if (!cc.enabled || !cc.pixel_count) continue;
        if (cc.pixel_count > kPixels) { err = ESP_ERR_INVALID_ARG; break; }
        candidate_shadow_[lane] = static_cast<AddressableShadowPixel*>(calloc(cc.pixel_count, sizeof(AddressableShadowPixel)));
        if (!candidate_shadow_[lane]) { err = kErrInsufficientRuntimeMemory; break; }
    }

    if (err == ESP_OK) {
        err = rmt_.prepare_candidate(cfg, generic_reserve, internal_reserve);
    }

    if (err != ESP_OK) {
        cancel_candidate();
        return err;
    }

    has_candidate_ = true;
    return ESP_OK;
}

esp_err_t AddressableDriver::commit_candidate() {
    if (!has_candidate_) return ESP_ERR_INVALID_STATE;

    esp_err_t err = rmt_.commit_candidate();
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        free_shadow(lane);
        shadow_[lane] = candidate_shadow_[lane];
        candidate_shadow_[lane] = nullptr;
    }
    has_candidate_ = false;
    return ESP_OK;
}

void AddressableDriver::cancel_candidate() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (candidate_shadow_[lane]) {
            free(candidate_shadow_[lane]);
            candidate_shadow_[lane] = nullptr;
        }
    }
    rmt_.cancel_candidate();
    has_candidate_ = false;
}

esp_err_t AddressableDriver::reconfigure(const DeviceConfig& cfg, uint32_t generic_reserve, uint32_t internal_reserve) {
    esp_err_t err = prepare_candidate(cfg, generic_reserve, internal_reserve);
    if (err != ESP_OK) return err;
    return commit_candidate();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

esp_err_t AddressableDriver::init() {
    ESP_LOGI(TAG, "Initialising AddressableDriver (%u lanes, max %u px/lane, parallel RMT)",
             kLanes, kPixels);
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        clear_lane(lane);
    }
    return ESP_OK;
}

// ── Lane configuration ────────────────────────────────────────────────────────

esp_err_t AddressableDriver::configure_lane(
    uint8_t lane, uint8_t gpio,
    FixtureProfile profile, PixelOrder order,
    uint16_t num_pixels)
{
    if (lane >= kLanes) {
        ESP_LOGE(TAG, "configure_lane: lane %u >= kLanes (%u)", lane, kLanes);
        return ESP_ERR_INVALID_ARG;
    }
    if (num_pixels > kPixels) {
        ESP_LOGE(TAG, "configure_lane: %u pixels exceeds maximum %u", num_pixels, kPixels);
        return ESP_ERR_INVALID_ARG;
    }

    // Tear down any existing lane first so state is always consistent with the
    // current (potentially zero-length) configuration.
    clear_lane(lane);

    if (num_pixels == 0) {
        ESP_LOGI(TAG, "Lane %u deconfigured (num_pixels=0)", lane);
        return ESP_OK;
    }

    const bool is_rgbw = (profile == FixtureProfile::SK6812);
    ESP_LOGI(TAG, "Configuring lane %u: GPIO %u, %u pixels, model=%s, order=%u",
             lane, gpio, num_pixels, is_rgbw ? "SK6812" : "WS2812", static_cast<uint8_t>(order));

    const esp_err_t err = rmt_.configure_lane(lane, gpio, is_rgbw, order, num_pixels);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure lane %u (GPIO %u): %s", lane, gpio, output_err_name(err));
        return err;
    }

    shadow_[lane] = static_cast<AddressableShadowPixel*>(calloc(num_pixels, sizeof(AddressableShadowPixel)));
    if (shadow_[lane] == nullptr) {
        rmt_.release_lane(lane);
        ESP_LOGE(TAG, "Lane %u shadow alloc failed: %s", lane, output_err_name(kErrRmtResourceExhausted));
        return kErrRmtResourceExhausted;
    }
    return ESP_OK;
}

// ── Buffer management ─────────────────────────────────────────────────────────

// clear() zeroes staged wire data and the diagnostic shadow. It does NOT transmit;
// call show()/blackout() to push zeros to the LEDs.
void AddressableDriver::clear() {
    rmt_.stage_clear_all();
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (shadow_[lane]) memset(shadow_[lane], 0, rmt_.pixels(lane) * sizeof(AddressableShadowPixel));
    }
}

// clear_lane() releases the RMT resources of this lane and resets all its state.
void AddressableDriver::clear_lane(uint8_t lane) {
    if (lane >= kLanes) return;
    rmt_.release_lane(lane);
    free_shadow(lane);
}

// clear_buffer() zeroes staged data without touching the hardware.
void AddressableDriver::clear_buffer() {
    clear();
}

// ── Per-pixel staging ─────────────────────────────────────────────────────────

void AddressableDriver::set(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes || !rmt_.configured(lane)) return;
    if (pixel >= rmt_.pixels(lane)) return; // bounds: configured length, not kPixels

    rmt_.stage_pixel(lane, pixel, r, g, b, w);

    if (shadow_[lane]) {
        // Shadow reflects what was actually staged: W only exists on RGBW lanes.
        shadow_[lane][pixel] = { r, g, b, rmt_.has_white(lane) ? w : uint8_t{0} };
    }
}

void AddressableDriver::fill(uint8_t lane, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes || !rmt_.configured(lane)) return;
    const uint16_t n = rmt_.pixels(lane);
    for (uint16_t i = 0; i < n; ++i) {
        set(lane, i, r, g, b, w);
    }
}

AddressableShadowPixel AddressableDriver::shadow_pixel(uint8_t lane, uint16_t pixel) const {
    if (lane >= kLanes || shadow_[lane] == nullptr || pixel >= rmt_.pixels(lane)) return {};
    return shadow_[lane][pixel];
}

// ── Hardware commit ───────────────────────────────────────────────────────────

// show() transmits staged data to ALL active lanes in parallel: every lane is
// submitted before any lane is waited on. See ParallelRmtDriver::commit().
AddressableCommitResult AddressableDriver::show() {
    return rmt_.commit(false);
}

// blackout() stages zeros and transmits them on every configured lane, including
// lanes previously marked Faulted. The caller inspects the result: a lane that
// cannot transmit a zero frame is NOT confirmed dark.
AddressableCommitResult AddressableDriver::blackout() {
    clear();
    return rmt_.commit(true);
}

} // namespace lg
