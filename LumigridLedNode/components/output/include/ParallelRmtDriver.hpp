// components/output/include/ParallelRmtDriver.hpp
//
// LG-038: LumiGrid-owned parallel addressable output driver.
//
// Commit contract (critical invariant):
//
//     submit A1 .. submit A8   -> every active lane is launched first
//     wait   A1 .. wait   A8   -> only then is any lane waited on
//
// The managed espressif__led_strip refresh path is submit+wait(-1) per strip, i.e.
// strictly serial. This driver never calls it. All RMT access goes through the
// IRmtTransport seam so the ordering can be proven by a fake transport in CI.
//
// This header intentionally has no ESP-IDF RMT dependency.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "SignageTypes.hpp"

namespace lg {

// ── Deterministic error codes ─────────────────────────────────────────────────
// Not an ESP-IDF error range; chosen to be unambiguous in logs and telemetry.
constexpr esp_err_t kErrRmtResourceExhausted = 0x6A01;
// LG-038.3 §41: explicit memory-admission failure (not a bare ESP_ERR_NO_MEM).
constexpr esp_err_t kErrInsufficientRuntimeMemory = 0x6A02;

/// Human readable name for output-path errors ("rmt_resource_exhausted", ...).
const char* output_err_name(esp_err_t err);

// ── Wire timing (800 kHz protocol, 1.25 us / bit) ─────────────────────────────
constexpr uint32_t kRmtResolutionHz         = 10 * 1000 * 1000;
constexpr uint32_t kAddressableResetUs      = 280;     // WS2812B-V5 compatible latch
/// Safe scheduling margin added to the predicted wire time before a wait times out.
/// Generous on purpose: FreeRTOS tick rounding can shorten a short timeout.
constexpr uint32_t kAddressableWaitMarginUs = 20000;
constexpr uint8_t  kNoFailedLane            = 0xFF;

/// Pure protocol data time only (no reset/latch). 256 RGB = 7680 us, RGBW = 10240 us.
constexpr uint32_t addressable_data_time_us(uint16_t pixels, bool rgbw) {
    return (static_cast<uint32_t>(pixels) * (rgbw ? 32u : 24u) * 5u + 3u) / 4u; // 1.25 us/bit
}

/// Predicted time on the wire for one lane frame: data + reset/latch.
constexpr uint32_t addressable_wire_time_us(uint16_t pixels, bool rgbw) {
    return addressable_data_time_us(pixels, rgbw) + kAddressableResetUs;
}

/// Bounded wait budget for one lane: predicted wire time + scheduling margin.
constexpr uint32_t addressable_timeout_us(uint16_t pixels, bool rgbw) {
    return addressable_wire_time_us(pixels, rgbw) + kAddressableWaitMarginUs;
}

enum class LaneState : uint8_t {
    Unconfigured = 0,
    Ready,
    Transmitting,
    Faulted,
};

const char* lane_state_name(LaneState s);

struct AddressableCommitResult {
    esp_err_t result       = ESP_OK;
    uint8_t   active_lanes = 0;
    uint32_t  submit_us    = 0;
    uint32_t  wait_us      = 0;
    uint32_t  total_us     = 0;
    uint8_t   failed_lane  = kNoFailedLane; // 0xFF when none
};

/// Thin seam over the ESP-IDF RMT TX API. Production implementation lives in
/// ParallelRmtDriver.cpp; tests install a recording fake.
class IRmtTransport {
public:
    virtual ~IRmtTransport() = default;
    /// Allocate TX channel + encoder for the lane and leave the channel enabled.
    virtual esp_err_t create(uint8_t lane, int gpio, bool rgbw) = 0;
    /// Queue a complete frame (data + latch). Must not block on wire completion.
    virtual esp_err_t submit(uint8_t lane, const uint8_t* data, size_t len) = 0;
    /// Bounded wait for the previously submitted frame.
    virtual esp_err_t wait(uint8_t lane, uint32_t timeout_us) = 0;
    /// Disable + delete channel and encoder.
    virtual esp_err_t destroy(uint8_t lane) = 0;
};

/// Production transport backed by the public ESP-IDF RMT TX driver.
IRmtTransport& production_rmt_transport();

class ParallelRmtDriver {
public:
    static constexpr uint8_t kLanes = MAX_RMT_LANES;

    explicit ParallelRmtDriver(IRmtTransport* transport = nullptr)
        : transport_(transport) {}
    ~ParallelRmtDriver() { release_all(); }

    ParallelRmtDriver(const ParallelRmtDriver&) = delete;
    ParallelRmtDriver& operator=(const ParallelRmtDriver&) = delete;

    /// Replace the transport. Only valid while no lane is configured.
    esp_err_t set_transport(IRmtTransport* transport);

    /// Allocate resources for one lane. `rgbw` selects 4 bytes/pixel (SK6812).
    /// Returns kErrRmtResourceExhausted when RMT channel/memory cannot be allocated.
    esp_err_t configure_lane(uint8_t lane, int gpio, bool rgbw, PixelOrder order, uint16_t pixels);
    void release_lane(uint8_t lane);
    void release_all();

    // ── Staging (no allocation, no locking) ──────────────────────────────────
    void stage_pixel(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    void stage_clear_lane(uint8_t lane);
    void stage_clear_all();

    /// Submit every active lane, then wait for every submitted lane. Faulted lanes are
    /// rejected (ESP_ERR_INVALID_STATE) unless `allow_faulted` is set (blackout recovery).
    AddressableCommitResult commit(bool allow_faulted = false);

    // ── Introspection ────────────────────────────────────────────────────────
    LaneState lane_state(uint8_t lane) const { return lane < kLanes ? lanes_[lane].state : LaneState::Unconfigured; }
    bool      configured(uint8_t lane) const { return lane_state(lane) != LaneState::Unconfigured; }
    uint16_t  pixels(uint8_t lane) const { return lane < kLanes ? lanes_[lane].pixels : 0; }
    bool      has_white(uint8_t lane) const { return lane < kLanes && lanes_[lane].rgbw; }
    uint8_t   active_lanes() const;
    /// Longest predicted wire time across active lanes (parallel budget), 0 when none.
    uint32_t  longest_wire_time_us() const;
    const uint8_t* wire_data(uint8_t lane) const { return lane < kLanes ? lanes_[lane].wire : nullptr; }
    size_t    wire_len(uint8_t lane) const { return lane < kLanes ? lanes_[lane].wire_len : 0; }
    /// Frame (wire) byte index for each logical component of this lane.
    uint8_t   pos_r(uint8_t lane) const { return lane < kLanes ? lanes_[lane].pos[0] : 0; }
    uint8_t   pos_g(uint8_t lane) const { return lane < kLanes ? lanes_[lane].pos[1] : 0; }
    uint8_t   pos_b(uint8_t lane) const { return lane < kLanes ? lanes_[lane].pos[2] : 0; }
    uint8_t   pos_w(uint8_t lane) const { return lane < kLanes ? lanes_[lane].pos[3] : 0; }
    /// Clear a Faulted lane back to Ready (after a confirmed good transmit or reconfigure).
    void      clear_faults();

private:
    struct Lane {
        LaneState state  = LaneState::Unconfigured;
        bool      rgbw   = false;
        uint16_t  pixels = 0;
        uint8_t   pos[4] = {0, 1, 2, 3}; // r,g,b,w -> wire byte index
        uint8_t*  wire   = nullptr;
        size_t    wire_len = 0;
    };

    IRmtTransport& tx() const;

    IRmtTransport* transport_ = nullptr;
    Lane lanes_[kLanes];
};

} // namespace lg
