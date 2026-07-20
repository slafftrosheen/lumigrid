#pragma once
#include <array>
#include <cstdint>
#include "esp_err.h"
#include "led_strip.h"
#include "FactoryApp.hpp"

namespace lg {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class AddressableLeds {
public:
    static constexpr uint8_t kLanes = 8;
    static constexpr uint16_t kPixels = LG_FACTORY_PIXELS_PER_LANE;

    esp_err_t init();

    void clear();
    void set(uint8_t lane, uint16_t pixel, Rgb color);
    void fill(uint8_t lane, Rgb color);
    void fill_all(Rgb color);

    esp_err_t show();
    esp_err_t blackout();

private:
    std::array<std::array<Rgb, kPixels>, kLanes> buffer_{};
    std::array<led_strip_handle_t, kLanes> strips_{};
};

} // namespace lg
