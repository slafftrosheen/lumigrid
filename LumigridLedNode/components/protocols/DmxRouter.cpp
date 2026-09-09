// components/protocols/DmxRouter.cpp
#include "DmxRouter.hpp"
#include "LightingRuntime.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

namespace lg {
namespace {
constexpr char kTag[] = "dmx_router";

inline uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
} // namespace

DmxRouter::DmxRouter(LightingRuntime& lighting)
    : lighting_(lighting) {}

DmxRouter::DmxRouter(LightingRuntime& lighting, const DeviceConfig& config)
    : lighting_(lighting) {
    update_config(config);
}

void DmxRouter::update_config(const DeviceConfig& config) {
    protocol_cfg_ = config.protocol;
    for (uint8_t f = 0; f < 16; ++f) {
        fixtures_[f] = config.fixtures[f];
    }
}

void DmxRouter::handle_artnet_dmx(const ArtnetDmxPacket& packet) {
    if (!protocol_cfg_.artnet_enabled) return;
    process_frame(packet.universe, packet.data, packet.length, 100);
}

void DmxRouter::handle_sacn_dmx(const SacnDmxPacket& packet) {
    if (!protocol_cfg_.sacn_enabled) return;
    process_frame(packet.universe, packet.data, packet.length, packet.priority);
}

DmxRouter::UniverseState* DmxRouter::get_or_create_universe(uint16_t universe) {
    const uint32_t current_ms = now_ms();
    const uint32_t stream_timeout = protocol_cfg_.stream_timeout_ms > 0 ? protocol_cfg_.stream_timeout_ms : 2000;

    UniverseState* oldest_slot = nullptr;
    uint32_t oldest_age = 0;

    for (auto& slot : tracked_universes_) {
        if (slot.active && slot.universe == universe) {
            return &slot;
        }
        if (!slot.active || (current_ms - slot.last_packet_ms > stream_timeout)) {
            if (!oldest_slot || (slot.last_packet_ms < oldest_age)) {
                oldest_slot = &slot;
                oldest_age = slot.last_packet_ms;
            }
        }
    }

    if (oldest_slot) {
        oldest_slot->universe = universe;
        oldest_slot->last_priority = 0;
        oldest_slot->last_packet_ms = 0;
        oldest_slot->merged_data.fill(0);
        oldest_slot->active = true;
        return oldest_slot;
    }

    return nullptr;
}

void DmxRouter::process_frame(uint16_t universe, const uint8_t* data, uint16_t length, uint8_t priority) {
    if (!data || length == 0) return;
    if (length > 512) length = 512;

    // Check if this universe is relevant:
    // Either it matches the zone wash universe (universe_start),
    // or it matches at least one patched fixture's dmx_universe.
    bool relevant = (universe == protocol_cfg_.universe_start);
    if (!relevant) {
        for (const auto& fix : fixtures_) {
            if (fix.dmx_start_channel > 0 && fix.dmx_universe == universe) {
                relevant = true;
                break;
            }
        }
    }
    if (!relevant) return;

    const uint32_t current_ms = now_ms();
    const uint32_t stream_timeout = protocol_cfg_.stream_timeout_ms > 0 ? protocol_cfg_.stream_timeout_ms : 2000;

    UniverseState* state = get_or_create_universe(universe);
    if (!state) return;

    // Priority preemption check:
    // If a current stream exists and has higher priority, and hasn't timed out, ignore this lower priority frame.
    if (state->last_packet_ms > 0 && (current_ms - state->last_packet_ms < stream_timeout)) {
        if (priority < state->last_priority) {
            return; // Preempted by higher priority source
        }
    }

    // Update stream priority and timestamp
    state->last_priority = priority;
    state->last_packet_ms = current_ms;

    // Channel Merging (HTP or LTP)
    if (protocol_cfg_.merge_mode == MergeMode::HTP) {
        for (uint16_t ch = 0; ch < length; ++ch) {
            state->merged_data[ch] = std::max(state->merged_data[ch], data[ch]);
        }
    } else {
        // LTP: Latest Takes Precedence
        memcpy(state->merged_data.data(), data, length);
    }

    // Forward raw DMX to LightingRuntime telemetry buffer
    lighting_.update_dmx_data(state->merged_data.data(), length);

    // Dispatch patched output
    dispatch_patch(universe, state->merged_data.data(), length);
}

void DmxRouter::dispatch_patch(uint16_t universe, const uint8_t* data, uint16_t length) {
    const uint8_t live_layer = protocol_cfg_.live_stream_layer < 3 ? protocol_cfg_.live_stream_layer : 2;

    // 1. Zone Wash Dispatch
    if (universe == protocol_cfg_.universe_start) {
        const uint16_t base_ch = (protocol_cfg_.zone_start_channel > 0) ? (protocol_cfg_.zone_start_channel - 1) : 0;
        for (uint8_t zone = 0; zone < 8; ++zone) {
            const size_t offset = base_ch + (zone * 3);
            if (offset + 2 >= length) break;

            BrandColor color{
                data[offset + 0],
                data[offset + 1],
                data[offset + 2]
            };
            lighting_.inject_live_data(zone, live_layer, color, 255);
        }
    }

    // 2. Direct Fixture Patch Dispatch
    for (uint8_t f = 0; f < 16; ++f) {
        const auto& fix = fixtures_[f];
        if (fix.dmx_start_channel == 0) continue; // Unpatched: inherits zone wash
        if (fix.dmx_universe != universe) continue;

        const size_t ch_offset = fix.dmx_start_channel - 1; // 0-indexed
        if (ch_offset >= length) continue;

        if (fix.dmx_mode == 0) {
            // Mode 0: Fixture Wash
            LightValue lv{0, 0, 0, 0};
            uint16_t pwm_duties[4] = {0, 0, 0, 0};

            if (fix.profile == FixtureProfile::PWM_Single) {
                uint8_t val = data[ch_offset];
                lv = {val, val, val, 0};
                pwm_duties[0] = (static_cast<uint32_t>(val) * 4095) / 255;
            } else if (fix.profile == FixtureProfile::PWM_RGB || fix.profile == FixtureProfile::WS2812) {
                if (ch_offset + 2 < length) {
                    lv = {data[ch_offset], data[ch_offset + 1], data[ch_offset + 2], 0};
                }
            } else if (fix.profile == FixtureProfile::PWM_RGBW || fix.profile == FixtureProfile::SK6812) {
                if (ch_offset + 3 < length) {
                    lv = {data[ch_offset], data[ch_offset + 1], data[ch_offset + 2], data[ch_offset + 3]};
                }
            }

            const uint16_t pcount = std::min<uint16_t>(fix.num_pixels, MAX_PIXELS);
            std::array<LightValue, MAX_PIXELS> px_buf;
            for (uint16_t p = 0; p < pcount; ++p) px_buf[p] = lv;
            lighting_.inject_live_fixture(f, px_buf.data(), pcount, pwm_duties, 255);
        } else if (fix.dmx_mode == 1) {
            // Mode 1: Direct Pixel Map
            const uint8_t ch_per_pixel = (fix.profile == FixtureProfile::SK6812 || fix.profile == FixtureProfile::PWM_RGBW) ? 4 : 3;
            const uint16_t pcount = std::min<uint16_t>(fix.num_pixels, MAX_PIXELS);
            std::array<LightValue, MAX_PIXELS> px_buf;

            for (uint16_t p = 0; p < pcount; ++p) {
                size_t px_ch = ch_offset + (p * ch_per_pixel);
                if (px_ch + ch_per_pixel - 1 < length) {
                    if (ch_per_pixel == 4) {
                        px_buf[p] = {data[px_ch], data[px_ch + 1], data[px_ch + 2], data[px_ch + 3]};
                    } else {
                        px_buf[p] = {data[px_ch], data[px_ch + 1], data[px_ch + 2], 0};
                    }
                } else {
                    px_buf[p] = {0, 0, 0, 0};
                }
            }
            lighting_.inject_live_fixture(f, px_buf.data(), pcount, nullptr, 255);
        }
    }
}

} // namespace lg
