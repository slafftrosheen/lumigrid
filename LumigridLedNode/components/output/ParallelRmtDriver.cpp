// components/output/ParallelRmtDriver.cpp
//
// LG-038: parallel addressable output on the public ESP-IDF RMT TX API.
// Does not use (or patch) managed_components/espressif__led_strip.
//
// Resource model, per configured lane:
//   1 RMT TX channel (kept enabled between frames) + 1 strip encoder + 1 wire byte buffer.
//
// Frame model:
//   stage -> submit all lanes -> wait all lanes (bounded) -> drain on failure.
#include "ParallelRmtDriver.hpp"

#include <cstdlib>
#include <cstring>

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lg_board_pins.hpp"

namespace lg {

static const char* TAG = "rmt_par";

const char* output_err_name(esp_err_t err) {
    if (err == kErrRmtResourceExhausted) return "rmt_resource_exhausted";
    if (err == kErrInsufficientRuntimeMemory) return "insufficient_runtime_memory";
    return esp_err_to_name(err);
}

const char* lane_state_name(LaneState s) {
    switch (s) {
        case LaneState::Unconfigured: return "unconfigured";
        case LaneState::Ready:        return "ready";
        case LaneState::Transmitting: return "transmitting";
        case LaneState::Faulted:      return "faulted";
    }
    return "unknown";
}

namespace {

#if CONFIG_RMT_ISR_IRAM_SAFE
#define LG_RMT_ENC_ATTR IRAM_ATTR
#else
#define LG_RMT_ENC_ATTR
#endif

// ── LumiGrid strip encoder: bytes encoder (pixel data) + copy encoder (latch) ───
struct StripEncoder {
    rmt_encoder_t base;          // must stay first: rmt calls through &base
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
};

LG_RMT_ENC_ATTR
size_t strip_encode(rmt_encoder_t* encoder, rmt_channel_handle_t channel,
                    const void* primary_data, size_t data_size, rmt_encode_state_t* ret_state) {
    auto* enc = reinterpret_cast<StripEncoder*>(encoder);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded = 0;
    switch (enc->state) {
        case 0:
            encoded += enc->bytes_encoder->encode(enc->bytes_encoder, channel, primary_data,
                                                  data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) enc->state = 1;
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_MEM_FULL);
                goto out;
            }
            [[fallthrough]];
        case 1:
            encoded += enc->copy_encoder->encode(enc->copy_encoder, channel, &enc->reset_code,
                                                 sizeof(enc->reset_code), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                enc->state = 0;
                state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_COMPLETE);
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_MEM_FULL);
                goto out;
            }
    }
out:
    *ret_state = state;
    return encoded;
}

esp_err_t strip_del(rmt_encoder_t* encoder) {
    auto* enc = reinterpret_cast<StripEncoder*>(encoder);
    if (enc->bytes_encoder) rmt_del_encoder(enc->bytes_encoder);
    if (enc->copy_encoder)  rmt_del_encoder(enc->copy_encoder);
    heap_caps_free(enc);
    return ESP_OK;
}

LG_RMT_ENC_ATTR
esp_err_t strip_reset(rmt_encoder_t* encoder) {
    auto* enc = reinterpret_cast<StripEncoder*>(encoder);
    rmt_encoder_reset(enc->bytes_encoder);
    rmt_encoder_reset(enc->copy_encoder);
    enc->state = 0;
    return ESP_OK;
}

esp_err_t new_strip_encoder(bool rgbw, rmt_encoder_handle_t* out) {
    // LG-HW-001: Explicitly allocate custom StripEncoder in internal DRAM
    auto* enc = static_cast<StripEncoder*>(
        heap_caps_calloc(1, sizeof(StripEncoder), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!enc) return ESP_ERR_NO_MEM;
    enc->base.encode = strip_encode;
    enc->base.del    = strip_del;
    enc->base.reset  = strip_reset;

    constexpr uint32_t res_mhz = kRmtResolutionHz / 1000000; // ticks per us
    auto sym = [](uint32_t high_ticks, uint32_t low_ticks) {
        rmt_symbol_word_t s = {};
        s.level0 = 1;
        s.duration0 = high_ticks;
        s.level1 = 0;
        s.duration1 = low_ticks;
        return s;
    };
    rmt_bytes_encoder_config_t cfg = {};
    cfg.bit0 = sym(res_mhz * 3 / 10, res_mhz * 9 / 10);          // T0H 0.3 us / T0L 0.9 us
    if (rgbw) {
        cfg.bit1 = sym(res_mhz * 6 / 10, res_mhz * 6 / 10);      // SK6812: T1H 0.6 us / T1L 0.6 us
    } else {
        cfg.bit1 = sym(res_mhz * 9 / 10, res_mhz * 3 / 10);      // WS2812: T1H 0.9 us / T1L 0.3 us
    }
    cfg.flags.msb_first = 1;

    esp_err_t err = rmt_new_bytes_encoder(&cfg, &enc->bytes_encoder);
    if (err != ESP_OK) { heap_caps_free(enc); return err; }
    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &enc->copy_encoder);
    if (err != ESP_OK) { rmt_del_encoder(enc->bytes_encoder); heap_caps_free(enc); return err; }

    // Latch/reset: two equal low halves (each duration is a 15-bit field).
    const uint32_t reset_ticks = res_mhz * kAddressableResetUs / 2;
    enc->reset_code = {};
    enc->reset_code.level0 = 0;
    enc->reset_code.duration0 = reset_ticks;
    enc->reset_code.level1 = 0;
    enc->reset_code.duration1 = reset_ticks;

    *out = &enc->base;
    return ESP_OK;
}

// ── Production transport ──────────────────────────────────────────────────────
class EspRmtTransport final : public IRmtTransport {
public:
    esp_err_t prepare(uint8_t lane, int gpio, bool rgbw, bool existing) override {
        if (!existing) return create(lane, gpio, rgbw);
        if (lane >= ParallelRmtDriver::kLanes || !chan_[lane] || pending_[lane])
            return ESP_ERR_INVALID_STATE;
        return new_strip_encoder(rgbw, &pending_[lane]);
    }
    void commit_prepared(uint8_t lane) override {
        if (pending_[lane]) {
            rmt_del_encoder(enc_[lane]);
            enc_[lane] = pending_[lane];
            pending_[lane] = nullptr;
        }
    }
    void cancel_prepared(uint8_t lane, bool existing) override {
        if (pending_[lane]) rmt_del_encoder(pending_[lane]);
        pending_[lane] = nullptr;
        if (!existing) destroy(lane);
    }
    esp_err_t create(uint8_t lane, int gpio, bool rgbw) override {
        if (lane >= ParallelRmtDriver::kLanes) return ESP_ERR_INVALID_ARG;
        if (chan_[lane] != nullptr) return ESP_ERR_INVALID_STATE;

        static bool s_policy_logged = false;
        if (!s_policy_logged) {
            s_policy_logged = true;
            ESP_LOGI(TAG, "RMT policy lanes=%u symbols=%u resolution=%lu priority=%d iram=%d",
                     ParallelRmtDriver::kLanes, static_cast<unsigned>(kRmtSymbolsPerLane),
                     (unsigned long)kRmtResolutionHz, kRmtInterruptPriority,
                     CONFIG_RMT_ISR_IRAM_SAFE ? 1 : 0);
        }

        rmt_tx_channel_config_t cc = {};
        cc.clk_src = RMT_CLK_SRC_DEFAULT;
        cc.gpio_num = static_cast<gpio_num_t>(gpio);
        cc.mem_block_symbols = 64;       // one ESP32 RMT block per lane (8 lanes = all 8 blocks)
        cc.resolution_hz = kRmtResolutionHz;
        cc.trans_queue_depth = 2;
        cc.intr_priority = kRmtInterruptPriority;
        cc.flags.with_dma = 0;
        cc.flags.invert_out = 0;

        const int core = xPortGetCoreID();
        esp_err_t err = rmt_new_tx_channel(&cc, &chan_[lane]);
        ESP_LOGI(TAG, "RMT_ALLOC lane=%u gpio=%d core=%d symbols=64 priority=%d result=%s",
                 lane, gpio, core, kRmtInterruptPriority, esp_err_to_name(err));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT channel allocation failed at priority %d for lane %u (gpio %d): %s",
                     kRmtInterruptPriority, lane, gpio, esp_err_to_name(err));
            chan_[lane] = nullptr;
            return err;
        }

        err = new_strip_encoder(rgbw, &enc_[lane]);
        if (err != ESP_OK) {
            rmt_del_channel(chan_[lane]);
            chan_[lane] = nullptr;
            enc_[lane] = nullptr;
            return err;
        }

        err = rmt_enable(chan_[lane]);   // stays enabled between frames
        if (err != ESP_OK) {
            rmt_del_encoder(enc_[lane]);
            rmt_del_channel(chan_[lane]);
            chan_[lane] = nullptr;
            enc_[lane] = nullptr;
            return err;
        }

        ESP_LOGI(TAG, "lane=%u gpio=%d created core=%d", lane, gpio, core);
        return ESP_OK;
    }

    esp_err_t submit(uint8_t lane, const uint8_t* data, size_t len) override {
        if (lane >= ParallelRmtDriver::kLanes || !chan_[lane]) return ESP_ERR_INVALID_STATE;
        rmt_transmit_config_t tc = {};
        tc.loop_count = 0;
        return rmt_transmit(chan_[lane], enc_[lane], data, len, &tc);
    }

    esp_err_t wait(uint8_t lane, uint32_t timeout_us) override {
        if (lane >= ParallelRmtDriver::kLanes || !chan_[lane]) return ESP_ERR_INVALID_STATE;
        const uint32_t ms = (timeout_us + 999u) / 1000u; // always bounded, never -1
        return rmt_tx_wait_all_done(chan_[lane], ms ? static_cast<int>(ms) : 1);
    }

    esp_err_t destroy(uint8_t lane) override {
        if (lane >= ParallelRmtDriver::kLanes || !chan_[lane]) return ESP_OK;
        esp_err_t first = rmt_disable(chan_[lane]);
        esp_err_t e2 = rmt_del_channel(chan_[lane]);
        if (first == ESP_OK) first = e2;
        if (enc_[lane]) rmt_del_encoder(enc_[lane]);
        chan_[lane] = nullptr;
        enc_[lane] = nullptr;
        return first;
    }

private:
    rmt_channel_handle_t chan_[ParallelRmtDriver::kLanes] = {};
    rmt_encoder_handle_t enc_[ParallelRmtDriver::kLanes] = {};
    rmt_encoder_handle_t pending_[ParallelRmtDriver::kLanes] = {};
};

EspRmtTransport s_production_transport;

esp_err_t map_create_error(esp_err_t err) {
    // No free channel / no memory: deterministic exhaustion code, never a silent downgrade.
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NO_MEM) return kErrRmtResourceExhausted;
    return err;
}

} // namespace

IRmtTransport& production_rmt_transport() { return s_production_transport; }

// ── ParallelRmtDriver ─────────────────────────────────────────────────────────

IRmtTransport& ParallelRmtDriver::tx() const {
    return transport_ ? *transport_ : production_rmt_transport();
}

esp_err_t ParallelRmtDriver::set_transport(IRmtTransport* transport) {
    if (active_lanes() != 0) return ESP_ERR_INVALID_STATE;
    transport_ = transport;
    return ESP_OK;
}

void ParallelRmtDriver::configure_pixel_order(Lane& lane, PixelOrder order) {
    const bool grb = (order == PixelOrder::GRB || order == PixelOrder::GRBW);
    lane.pos[0] = grb ? 1 : 0;
    lane.pos[1] = grb ? 0 : 1;
    lane.pos[2] = 2;
    lane.pos[3] = 3;
}

esp_err_t ParallelRmtDriver::configure_lane(uint8_t lane, int gpio, bool rgbw,
                                            PixelOrder order, uint16_t pixels) {
    if (lane >= kLanes || pixels > MAX_PIXELS) return ESP_ERR_INVALID_ARG;
    if (!pixel_order_valid(rgbw ? AddressableType::SK6812 : AddressableType::WS2812, order)) {
        return ESP_ERR_INVALID_ARG;
    }
    release_lane(lane);
    if (pixels == 0) return ESP_OK;

    Lane& L = lanes_[lane];
    const size_t bpp = rgbw ? 4u : 3u;
    L.wire_len = bpp * pixels;
    // LG-HW-001: Force RMT ISR-accessed wire buffers into internal DRAM
    L.wire = static_cast<uint8_t*>(
        heap_caps_calloc(1, L.wire_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!L.wire) {
        L.wire_len = 0;
        ESP_LOGE(TAG, "lane %u: wire buffer alloc failed (%s)", lane, output_err_name(kErrRmtResourceExhausted));
        return kErrRmtResourceExhausted;
    }

    configure_pixel_order(L, order);
    L.rgbw = rgbw;
    L.pixels = pixels;
    diagnostics_.allocation_core = xPortGetCoreID();

    const esp_err_t err = tx().create(lane, gpio, rgbw);
    if (err != ESP_OK) {
        heap_caps_free(L.wire);
        L = Lane{};
        const esp_err_t mapped = map_create_error(err);
        ESP_LOGE(TAG, "lane %u (GPIO %d): channel create failed: %s", lane, gpio, output_err_name(mapped));
        return mapped;
    }
    L.state = LaneState::Ready;
    return ESP_OK;
}

void ParallelRmtDriver::release_lane(uint8_t lane) {
    if (lane >= kLanes) return;
    Lane& L = lanes_[lane];
    if (L.state != LaneState::Unconfigured) {
        tx().destroy(lane);
    }
    heap_caps_free(L.wire);
    L = Lane{};
}

void ParallelRmtDriver::release_all() {
    for (uint8_t i = 0; i < kLanes; ++i) release_lane(i);
}

esp_err_t ParallelRmtDriver::prepare_candidate(const DeviceConfig& cfg, uint32_t generic_reserve, uint32_t internal_reserve) {
    cancel_candidate();

    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (!configured(lane)) continue;
        const esp_err_t err = tx().wait(lane, addressable_timeout_us(lanes_[lane].pixels, lanes_[lane].rgbw));
        if (err != ESP_OK) return err;
    }

    esp_err_t err = ESP_OK;
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        const auto& cc = cfg.channels[lane];
        if (!cc.enabled || !cc.pixel_count) continue;
        if (cc.pixel_count > MAX_PIXELS) { err = ESP_ERR_INVALID_ARG; break; }
        if (!pixel_order_valid(cc.addressable_type, cc.pixel_order)) { err = ESP_ERR_INVALID_ARG; break; }
        auto& next = candidate_[lane];
        next.rgbw = cc.addressable_type == AddressableType::SK6812;
        next.pixels = cc.pixel_count;
        next.wire_len = next.pixels * (next.rgbw ? 4u : 3u);
        // LG-HW-001: Internal DRAM wire buffer
        next.wire = static_cast<uint8_t*>(
            heap_caps_calloc(1, next.wire_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!next.wire) { err = kErrInsufficientRuntimeMemory; break; }
        configure_pixel_order(next, cc.pixel_order);
        next.state = LaneState::Ready;
        err = tx().prepare(lane, board::kAledPins[lane], next.rgbw, configured(lane));
        if (err != ESP_OK) { err = map_create_error(err); break; }
        candidate_prepared_[lane] = true;
    }

    // Old resources remain live until all candidate allocations and both internal
    // and generic reserve gates pass.
    if (err == ESP_OK) {
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < internal_reserve ||
            heap_caps_get_free_size(MALLOC_CAP_8BIT) < generic_reserve) {
            err = kErrInsufficientRuntimeMemory;
        }
    }

    if (err != ESP_OK) {
        cancel_candidate();
        return err;
    }

    has_candidate_ = true;
    return ESP_OK;
}

esp_err_t ParallelRmtDriver::commit_candidate() {
    if (!has_candidate_) return ESP_ERR_INVALID_STATE;

    diagnostics_.allocation_core = xPortGetCoreID();
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (candidate_prepared_[lane]) {
            tx().commit_prepared(lane);
        } else if (configured(lane)) {
            tx().destroy(lane);
        }
        heap_caps_free(lanes_[lane].wire);
        lanes_[lane] = candidate_[lane];
        candidate_[lane] = Lane{};
        candidate_prepared_[lane] = false;
    }
    has_candidate_ = false;
    return ESP_OK;
}

void ParallelRmtDriver::cancel_candidate() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (candidate_prepared_[lane]) {
            tx().cancel_prepared(lane, configured(lane));
            candidate_prepared_[lane] = false;
        }
        if (candidate_[lane].wire) {
            heap_caps_free(candidate_[lane].wire);
            candidate_[lane].wire = nullptr;
        }
        candidate_[lane] = Lane{};
    }
    has_candidate_ = false;
}

esp_err_t ParallelRmtDriver::reconfigure(const DeviceConfig& cfg, uint32_t generic_reserve, uint32_t internal_reserve) {
    esp_err_t err = prepare_candidate(cfg, generic_reserve, internal_reserve);
    if (err != ESP_OK) return err;
    return commit_candidate();
}

void ParallelRmtDriver::stage_pixel(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes) return;
    Lane& L = lanes_[lane];
    if (L.wire == nullptr || pixel >= L.pixels) return;
    uint8_t* p = L.wire + static_cast<size_t>(pixel) * (L.rgbw ? 4u : 3u);
    p[L.pos[0]] = r;
    p[L.pos[1]] = g;
    p[L.pos[2]] = b;
    if (L.rgbw) p[L.pos[3]] = w;
}

void ParallelRmtDriver::stage_clear_lane(uint8_t lane) {
    if (lane >= kLanes || lanes_[lane].wire == nullptr) return;
    memset(lanes_[lane].wire, 0, lanes_[lane].wire_len);
}

void ParallelRmtDriver::stage_clear_all() {
    for (uint8_t i = 0; i < kLanes; ++i) stage_clear_lane(i);
}

uint8_t ParallelRmtDriver::active_lanes() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kLanes; ++i) if (lanes_[i].state != LaneState::Unconfigured) ++n;
    return n;
}

uint32_t ParallelRmtDriver::longest_wire_time_us() const {
    uint32_t longest = 0;
    for (uint8_t i = 0; i < kLanes; ++i) {
        if (lanes_[i].state == LaneState::Unconfigured) continue;
        const uint32_t t = addressable_wire_time_us(lanes_[i].pixels, lanes_[i].rgbw);
        if (t > longest) longest = t;
    }
    return longest;
}

void ParallelRmtDriver::clear_faults() {
    for (uint8_t i = 0; i < kLanes; ++i) {
        if (lanes_[i].state == LaneState::Faulted) lanes_[i].state = LaneState::Ready;
    }
}

AddressableCommitResult ParallelRmtDriver::commit(bool allow_faulted) {
    AddressableCommitResult res;
    const int64_t t0 = esp_timer_get_time();

    diagnostics_.total_frames++;
    bool submitted[kLanes] = {};
    auto note = [&res](esp_err_t err, uint8_t lane) {
        if (res.result == ESP_OK) { res.result = err; res.failed_lane = lane; }
    };

    static esp_err_t s_last_sub_err[kLanes] = {};
    static esp_err_t s_last_wait_err[kLanes] = {};

    // Phase 1: submit EVERY active lane before waiting on ANY lane.
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        Lane& L = lanes_[lane];
        if (L.state == LaneState::Unconfigured) continue;
        ++res.active_lanes;
        if (L.state == LaneState::Faulted && !allow_faulted) {
            note(ESP_ERR_INVALID_STATE, lane);
            continue;
        }

        const int64_t sub_t0 = esp_timer_get_time();
        const esp_err_t err = tx().submit(lane, L.wire, L.wire_len);
        const uint32_t sub_us = static_cast<uint32_t>(esp_timer_get_time() - sub_t0);

        auto& ldiag = diagnostics_.lanes[lane];
        ldiag.submissions++;
        if (sub_us > ldiag.max_submit_us) ldiag.max_submit_us = sub_us;

        if (err != ESP_OK) {
            L.state = LaneState::Faulted;
            note(err, lane);
            ldiag.submit_failures++;
            ldiag.last_error = err;
            if (s_last_sub_err[lane] != err) {
                s_last_sub_err[lane] = err;
                ESP_LOGE(TAG, "lane %u submit failed: %s", lane, output_err_name(err));
            }
            continue;
        }
        s_last_sub_err[lane] = ESP_OK;
        L.state = LaneState::Transmitting;
        submitted[lane] = true;
    }
    const int64_t t1 = esp_timer_get_time();

    // Phase 2: bounded waits. A failed wait never abandons the remaining lanes:
    // every submitted lane is waited on so nothing is left in flight.
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (!submitted[lane]) continue;
        Lane& L = lanes_[lane];
        const int64_t wait_t0 = esp_timer_get_time();
        const esp_err_t err = tx().wait(lane, addressable_timeout_us(L.pixels, L.rgbw));
        const uint32_t wait_us = static_cast<uint32_t>(esp_timer_get_time() - wait_t0);

        auto& ldiag = diagnostics_.lanes[lane];
        if (wait_us > ldiag.max_wait_us) ldiag.max_wait_us = wait_us;

        if (err != ESP_OK) {
            L.state = LaneState::Faulted;
            note(err, lane);
            if (err == ESP_ERR_TIMEOUT) {
                ldiag.wait_timeouts++;
            } else {
                ldiag.other_failures++;
            }
            ldiag.last_error = err;
            if (s_last_wait_err[lane] != err) {
                s_last_wait_err[lane] = err;
                ESP_LOGE(TAG, "lane %u wait failed: %s", lane, output_err_name(err));
            }
        } else {
            L.state = LaneState::Ready;
            ldiag.completed++;
            ldiag.last_error = ESP_OK;
            s_last_wait_err[lane] = ESP_OK;
        }
    }
    const int64_t t2 = esp_timer_get_time();

    res.submit_us = static_cast<uint32_t>(t1 - t0);
    res.wait_us   = static_cast<uint32_t>(t2 - t1);
    res.total_us  = static_cast<uint32_t>(t2 - t0);

    if (res.result != ESP_OK) {
        diagnostics_.failed_frames++;
    }

    return res;
}

RmtDiagnostics ParallelRmtDriver::diagnostics() const {
    diagnostics_.active_lanes = active_lanes();
    diagnostics_.interrupt_priority = kRmtInterruptPriority;
    diagnostics_.iram_safe = (CONFIG_RMT_ISR_IRAM_SAFE != 0);
    return diagnostics_;
}

} // namespace lg
