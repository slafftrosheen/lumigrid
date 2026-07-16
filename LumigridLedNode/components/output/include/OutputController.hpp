#pragma once
#include "AddressableLeds.hpp"
#include "Pca9685.hpp"

namespace lg {

class OutputController {
public:
    esp_err_t init();
    esp_err_t blackout();
    esp_err_t commit();

    AddressableLeds& pixels() { return pixels_; }
    Pca9685& pwm() { return pwm_; }

    void enable();
    void disable();
    bool enabled() const { return enabled_; }

private:
    AddressableLeds pixels_;
    Pca9685 pwm_;
    bool enabled_ = false;
};

} // namespace lg
