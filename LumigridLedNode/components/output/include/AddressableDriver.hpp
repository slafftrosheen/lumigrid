#pragma once
#include <cstdint>
#include "esp_err.h"
#include "SignageTypes.hpp"
#include "FactoryApp.hpp"
#include "ParallelRmtDriver.hpp"

namespace lg {

/// Diagnostic mirror of the values actually staged for the hardware, including W.
/// Observational only: fixed-size, no per-frame allocation, no locking per pixel.
struct AddressableShadowPixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
};

/// Addressable (RMT) output facade. All transmission is delegated to ParallelRmtDriver:
/// every active lane is submitted before any lane is waited on.
class AddressableDriver {
public:
    esp_err_t reconfigure(const DeviceConfig& cfg, uint32_t generic_reserve, uint32_t internal_reserve = kPostTopologyInternalReserveBytes);
    esp_err_t prepare_candidate(const DeviceConfig& cfg, uint32_t generic_reserve, uint32_t internal_reserve = kPostTopologyInternalReserveBytes);
    esp_err_t commit_candidate();
    void cancel_candidate();
    bool has_candidate() const { return has_candidate_; }
    static constexpr uint8_t kLanes = MAX_RMT_LANES;
    /// Same canonical product maximum as config validation. There is no separate limit.
    static constexpr uint16_t kPixels = MAX_PIXELS;

    AddressableDriver() = default;
    ~AddressableDriver();
    AddressableDriver(const AddressableDriver&) = delete;
    AddressableDriver& operator=(const AddressableDriver&) = delete;

    esp_err_t init();

    /// Reconfigure a specific lane. Returns kErrRmtResourceExhausted (never a silent
    /// downgrade) when RMT resources cannot be allocated.
    esp_err_t configure_lane(uint8_t lane, uint8_t gpio, FixtureProfile profile, PixelOrder order, uint16_t num_pixels);

    void clear();
    esp_err_t clear_lane(uint8_t lane);
    void clear_buffer();
    void set(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    void fill(uint8_t lane, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);

    /// Parallel commit of all staged lanes.
    AddressableCommitResult show();
    /// Stage zeros and transmit them on every configured lane (including faulted ones).
    AddressableCommitResult blackout();

    // ── Lane metadata ────────────────────────────────────────────────────────
    uint16_t configured_pixels(uint8_t lane) const { return rmt_.pixels(lane); }
    bool has_white(uint8_t lane) const { return rmt_.has_white(lane); }
    bool configured(uint8_t lane) const { return rmt_.configured(lane); }
    LaneState lane_state(uint8_t lane) const { return rmt_.lane_state(lane); }
    uint8_t active_lanes() const { return rmt_.active_lanes(); }
    uint32_t longest_wire_time_us() const { return rmt_.longest_wire_time_us(); }
    void scale_lane(uint8_t lane, uint16_t scale_256) { rmt_.scale_lane(lane, scale_256); }
    const uint8_t* wire_data(uint8_t lane) const { return rmt_.wire_data(lane); }

    /// Diagnostic shadow. Returns zeros outside the configured lane/pixel range.
    AddressableShadowPixel shadow_pixel(uint8_t lane, uint16_t pixel) const;

    /// Test seam: install a fake RMT transport. Only valid while no lane is configured.
    esp_err_t use_transport(IRmtTransport* transport) { return rmt_.set_transport(transport); }
    const ParallelRmtDriver& rmt() const { return rmt_; }
    RmtDiagnostics diagnostics() const { return rmt_.diagnostics(); }

private:
    void free_shadow(uint8_t lane);

    ParallelRmtDriver rmt_;
    // Per-lane shadow, allocated at configure time (never per frame).
    AddressableShadowPixel* shadow_[kLanes] = {};
    AddressableShadowPixel* candidate_shadow_[kLanes] = {};
    bool has_candidate_ = false;
};

} // namespace lg
