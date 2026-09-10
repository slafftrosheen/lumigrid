#pragma once
#include <array>
#include <cstdint>
#include "esp_err.h"
#include "led_strip.h"
#include "SignageTypes.hpp"
#include "FactoryApp.hpp"

namespace lg {

class AddressableDriver {
public:
    static constexpr uint8_t kLanes = 8;
    static constexpr uint16_t kPixels = 144; // Max pixels for factory preview

    esp_err_t init();
    
    // Reconfigure a specific lane with the proper physical parameters
    esp_err_t configure_lane(uint8_t lane, uint8_t gpio, FixtureProfile profile, PixelOrder order, uint16_t num_pixels);

    void clear();
    void clear_buffer();
    void set(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    
    esp_err_t show();
    esp_err_t blackout();
    
    void fill(uint8_t lane, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
    const std::array<std::array<BrandColor, kPixels>, kLanes>& buffer() const { return buffer_; }

private:
    std::array<led_strip_handle_t, kLanes> strips_{};
    std::array<bool, kLanes> has_white_{}; // true if SK6812 RGBW
    std::array<uint16_t, kLanes> num_pixels_{};
    std::array<std::array<BrandColor, kPixels>, kLanes> buffer_{};
};

} // namespace lg
