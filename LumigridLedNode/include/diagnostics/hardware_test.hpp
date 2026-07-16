#pragma once
#include <cstdint>
#include <string>
#include "frame/lg_frame.hpp"

namespace diagnostics {

enum class TestPattern {
    None,
    LaneSolid,
    LaneChase,
    LaneGradient,
    AllLanesIdentity,
    PwmLevel,
    PwmRamp,
    PwmWalking,
    BurnIn,
    EmergencyBlackout
};

struct HardwareTestRequest {
    TestPattern pattern = TestPattern::None;
    int lane = 0;
    int channel = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    int brightness = 255;
    uint32_t durationMs = 15000;
};

void hardwareTestInit();
void hardwareTestTick(lg::LogicalFrame& frame, uint32_t now_ms);
bool hardwareTestStart(const HardwareTestRequest& req, std::string* outResult);
void hardwareTestAbort();
bool isHardwareTestRunning();

}

