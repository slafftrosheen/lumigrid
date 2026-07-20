// components/factory/include/FactoryEffects.hpp
#pragma once
#include <cstdint>

#include "FactoryTypes.hpp"
#include "OutputController.hpp"

namespace lg {

const char* effect_name(FactoryEffect effect);

void render_effect(
    FactoryEffect effect,
    OutputController& output,
    uint32_t elapsed_ms,
    uint8_t brightness
);

} // namespace lg
