#pragma once
#include "FactoryEffects.hpp"
#include "OutputController.hpp"
#include "../../../main/FactoryApp.hpp"

namespace lg {

class FactoryRunner {
public:
    explicit FactoryRunner(OutputController& output);

    void start();
    void select(FactoryEffect effect, uint32_t duration_ms, uint8_t brightness);
    void stop();

    OutputController& output() { return output_; }
    FactoryEffect active_effect() const { return active_; }

private:
    static void task_entry(void* argument);
    void task();

    OutputController& output_;
    FactoryEffect active_ = FactoryEffect::Identity;
    uint32_t started_ms_ = 0;
    uint32_t duration_ms_ = 0;
    uint8_t brightness_ = LG_FACTORY_DEFAULT_BRIGHTNESS;
    bool auto_loop_ = true;
};

} // namespace lg
