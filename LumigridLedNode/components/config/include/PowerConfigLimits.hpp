// components/config/include/PowerConfigLimits.hpp
#pragma once
#include <cstdint>

namespace lg {

// Explicit numeric quantities for electrical modeling (LG-HW-005.1)
using PowerMilliwatts = uint32_t;
using VoltageMillivolts = uint32_t;
using CurrentMilliamps = uint32_t;

// Software numeric ceiling limits for configuration validation.
// Note: These represent software data-model ceilings, not physically qualified board ratings.
struct PowerConfigLimits {
    static constexpr PowerMilliwatts kMaxGlobalPowerMw = 2'000'000;      // 2,000 W software limit
    static constexpr PowerMilliwatts kMaxChannelPowerMw = 1'000'000;     // 1,000 W per channel software limit
    static constexpr VoltageMillivolts kMinSupplyVoltageMv = 3'000;      // 3 V min logical PSU voltage
    static constexpr VoltageMillivolts kMaxSupplyVoltageMv = 60'000;     // 60 V max logical PSU voltage
    static constexpr CurrentMilliamps kMaxSupplyCurrentMa = 200'000;     // 200 A max logical PSU rating
    static constexpr uint16_t kMaxUsableCapacityPermille = 1000;         // 100% max derating
};

inline constexpr PowerMilliwatts kMaxGlobalPowerMw = PowerConfigLimits::kMaxGlobalPowerMw;
inline constexpr PowerMilliwatts kMaxChannelPowerMw = PowerConfigLimits::kMaxChannelPowerMw;
inline constexpr VoltageMillivolts kMinSupplyVoltageMv = PowerConfigLimits::kMinSupplyVoltageMv;
inline constexpr VoltageMillivolts kMaxSupplyVoltageMv = PowerConfigLimits::kMaxSupplyVoltageMv;
inline constexpr CurrentMilliamps kMaxSupplyCurrentMa = PowerConfigLimits::kMaxSupplyCurrentMa;
inline constexpr uint16_t kMaxUsableCapacityPermille = PowerConfigLimits::kMaxUsableCapacityPermille;

} // namespace lg
