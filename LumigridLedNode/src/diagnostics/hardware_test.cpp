#include "diagnostics/hardware_test.hpp"
#include "esp_log.h"
#include <Arduino.h>

namespace diagnostics {

static const char* TAG = "hwtest";
static volatile bool g_hwtest_running = false;
static HardwareTestRequest g_req;
static uint32_t g_start_ms = 0;

void hardwareTestInit() { g_hwtest_running = false; }

bool isHardwareTestRunning() { return g_hwtest_running; }

void hardwareTestTick(lg::LogicalFrame& frame, uint32_t now_ms) {
    if (!g_hwtest_running) return;

    if (now_ms - g_start_ms > g_req.durationMs) {
        g_hwtest_running = false;
        frame.pixel_count = 0; // Blackout
        frame.pixels.fill({0,0,0});
        frame.pwm.fill(0);
        return;
    }

    switch (g_req.pattern) {
        case TestPattern::LaneSolid:
            frame.pixel_count = lg::kMaxLogicalPixels;
            for (size_t i = g_req.lane * lg::kMaxPixelsPerLane; i < (g_req.lane + 1) * lg::kMaxPixelsPerLane; ++i) {
                frame.pixels[i] = {static_cast<uint8_t>(g_req.r), static_cast<uint8_t>(g_req.g), static_cast<uint8_t>(g_req.b)};
            }
            break;
        case TestPattern::EmergencyBlackout:
            g_hwtest_running = false;
            frame.pixel_count = 0;
            frame.pixels.fill({0,0,0});
            frame.pwm.fill(0);
            break;
        default:
            // Minimal implementation for others to satisfy build/API contract for now.
            break;
    }
}

bool hardwareTestStart(const HardwareTestRequest& req, std::string* outResult) {
    g_req = req;
    g_start_ms = millis();
    g_hwtest_running = true;
    if (outResult) *outResult = "started";
    return true;
}

void hardwareTestAbort() { g_hwtest_running = false; }

}
