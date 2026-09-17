// components/output/AddressableDriver.cpp
//
// Manages up to kLanes independent RMT-driven addressable LED strips (WS2812/SK6812).
// Built on the ESP-IDF led_strip component (v5.x), which handles RMT encoding internally.
//
// RMT resolution: 10 MHz (100 ns per symbol). WS2812 timing fits within ±150 ns tolerance.
// SK6812 RGBW: 4-byte data order; the LED strip component handles W-channel placement.
//
// Pixel ordering is per-fixture and specified via PixelOrder:
//   RGB → LED_STRIP_COLOR_COMPONENT_FMT_RGB
//   GRB → LED_STRIP_COLOR_COMPONENT_FMT_GRB   (WS2812 default)
//   RGBW, GRBW → require SK6812 model for correct hardware decoding.
//
#include "AddressableDriver.hpp"
#include "lg_board_pins.hpp"

#include "led_strip.h"
#include "esp_check.h"
#include "esp_log.h"
#include <cstring>

namespace lg {

static const char* TAG = "aled";

// ── Lifecycle ─────────────────────────────────────────────────────────────────

esp_err_t AddressableDriver::init() {
    ESP_LOGI(TAG, "Initialising AddressableDriver (%u lanes, max %u px/lane)",
             kLanes, kPixels);
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        strips_[lane]      = nullptr;
        has_white_[lane]   = false;
        num_pixels_[lane]  = 0;
    }
    buffer_ = {};
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

    // ── Tear down any existing strip on this lane first ───────────────────────
    if (strips_[lane] != nullptr) {
        led_strip_del(strips_[lane]);
        strips_[lane] = nullptr;
    }

    // Reset lane metadata. Must be done here (not after the early return) so that
    // previously-set has_white_ and num_pixels_ are always consistent with the
    // current (potentially zero) strip configuration.
    has_white_[lane]  = false;
    num_pixels_[lane] = num_pixels;

    // Clear software buffer for this lane regardless of whether a strip is
    // installed, so stale pixel data is never surfaced by the visualizer.
    for (uint16_t p = 0; p < kPixels; ++p) {
        buffer_[lane][p] = {};
    }

    if (num_pixels == 0) {
        ESP_LOGI(TAG, "Lane %u deconfigured (num_pixels=0)", lane);
        return ESP_OK;
    }

    // ── Resolve LED model and pixel format ────────────────────────────────────
    const bool is_rgbw = (profile == FixtureProfile::SK6812);
    if (is_rgbw) {
        has_white_[lane] = true;
    }

    // led_model_t selects the physical signal timing:
    //   LED_MODEL_WS2812 → WS2812/WS2812B/WS2811 family (3 data bytes)
    //   LED_MODEL_SK6812  → SK6812/SK6812RGBW      (4 data bytes for RGBW)
    const led_model_t model = is_rgbw ? LED_MODEL_SK6812 : LED_MODEL_WS2812;

    // Color component format maps to the wire byte order expected by the LED IC.
    // GRB is the default for WS2812; SK6812 is typically GRBW.
    led_color_component_format_t format;
    switch (order) {
        case PixelOrder::RGB:  format = LED_STRIP_COLOR_COMPONENT_FMT_RGB;  break;
        case PixelOrder::GRB:  format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;  break;
        case PixelOrder::RGBW: format = LED_STRIP_COLOR_COMPONENT_FMT_RGBW; break;
        case PixelOrder::GRBW: format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW; break;
        default:               format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;  break;
    }

    // ── Create the led_strip RMT device ──────────────────────────────────────
    const led_strip_config_t strip_cfg = {
        .strip_gpio_num        = gpio,
        .max_leds              = static_cast<uint32_t>(num_pixels),
        .led_model             = model,
        .color_component_format = format,
        .flags                 = { .invert_out = false },
    };

    // mem_block_symbols: RMT memory block allocated for this channel.
    // Each encoded LED bit uses 2 RMT symbols (16-bit entries), so per LED:
    //   WS2812: 24 bits → 48 symbols
    //   SK6812: 32 bits → 64 symbols
    // With DMA disabled, the RMT hardware sends from its internal SRAM;
    // the led_strip component re-fills per-call. 64 symbols is the minimum
    // safe block size and avoids over-allocating the shared RMT SRAM.
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src          = RMT_CLK_SRC_DEFAULT,
        .resolution_hz    = 10 * 1000 * 1000, // 10 MHz → 100 ns resolution (WS2812 ±150 ns)
        .mem_block_symbols = 64,
        .flags             = { .with_dma = false },
    };

    ESP_LOGI(TAG, "Configuring lane %u: GPIO %d, %u pixels, model=%s, order=%u",
             lane, gpio, num_pixels, is_rgbw ? "SK6812" : "WS2812", static_cast<uint8_t>(order));

    const esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strips_[lane]);
    if (err != ESP_OK) {
        // Roll back state so this lane appears unconfigured.
        has_white_[lane]  = false;
        num_pixels_[lane] = 0;
        ESP_LOGE(TAG, "Failed to create RMT strip on lane %u (GPIO %d): %s",
                 lane, gpio, esp_err_to_name(err));
    }
    return err;
}

// ── Buffer management ─────────────────────────────────────────────────────────

// clear() zeroes the internal led_strip encoder buffer and our shadow buffer.
// It does NOT transmit to the hardware — call show() separately to blank the LEDs.
void AddressableDriver::clear() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (strips_[lane]) {
            led_strip_clear(strips_[lane]); // zeroes encoder's internal pixel buffer
        }
        for (uint16_t p = 0; p < kPixels; ++p) {
            buffer_[lane][p] = {};
        }
    }
}

// clear_lane() tears down the physical strip on this lane and resets all state.
// Call this when deconfiguring a lane between reconfigure() calls.
void AddressableDriver::clear_lane(uint8_t lane) {
    if (lane >= kLanes) return;
    if (strips_[lane] != nullptr) {
        led_strip_del(strips_[lane]);
        strips_[lane] = nullptr;
    }
    has_white_[lane]  = false;
    num_pixels_[lane] = 0;
    for (uint16_t p = 0; p < kPixels; ++p) {
        buffer_[lane][p] = {};
    }
}

// clear_buffer() resets the software shadow buffer without touching the hardware.
void AddressableDriver::clear_buffer() {
    buffer_ = {};
}

// ── Per-pixel staging ─────────────────────────────────────────────────────────

void AddressableDriver::set(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes || strips_[lane] == nullptr) return;

    // Bounds-check against the actual configured pixel count, not kPixels.
    // Prevents writing past the end of what led_strip_new_rmt_device() allocated.
    if (pixel >= num_pixels_[lane]) return;

    if (has_white_[lane]) {
        led_strip_set_pixel_rgbw(strips_[lane], pixel, r, g, b, w);
    } else {
        led_strip_set_pixel(strips_[lane], pixel, r, g, b);
    }

    // Mirror to the shadow buffer for the visualizer (bounded by kPixels).
    if (pixel < kPixels) {
        buffer_[lane][pixel] = { r, g, b };
    }
}

void AddressableDriver::fill(uint8_t lane, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes || strips_[lane] == nullptr) return;
    for (uint16_t i = 0; i < num_pixels_[lane]; ++i) {
        set(lane, i, r, g, b, w);
    }
}

// ── Hardware commit ───────────────────────────────────────────────────────────

// show() transmits the current encoder buffer to all active lanes.
// Lanes without a strip are skipped. Stops on first error.
esp_err_t AddressableDriver::show() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (strips_[lane] == nullptr) continue;
        ESP_RETURN_ON_ERROR(
            led_strip_refresh(strips_[lane]),
            TAG, "refresh lane %u", lane
        );
    }
    return ESP_OK;
}

// blackout() clears all channels and immediately transmits zeros to hardware.
esp_err_t AddressableDriver::blackout() {
    clear();
    return show();
}

} // namespace lg
