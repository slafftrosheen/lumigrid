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
    std::lock_guard<std::mutex> lock(frame_mutex_);
    protocol_cfg_ = config.protocol;
    for (uint8_t f = 0; f < 16; ++f) {
        fixtures_[f] = config.fixtures[f];
    }
}

size_t DmxRouter::get_active_universes(uint16_t* out_universes, size_t max_count) const {
    if (!out_universes || max_count == 0) return 0;

    std::lock_guard<std::mutex> lock(frame_mutex_);
    size_t count = 0;
    auto add_universe = [&](uint16_t u) {
        for (size_t i = 0; i < count; ++i) {
            if (out_universes[i] == u) return;
        }
        if (count < max_count) {
            out_universes[count++] = u;
        }
    };

    add_universe(protocol_cfg_.universe_start);

    for (const auto& fix : fixtures_) {
        if (fix.dmx_start_channel > 0) {
            add_universe(fix.dmx_universe);
        }
    }
    return count;
}

void DmxRouter::handle_artnet_dmx(const ArtnetDmxPacket& packet) {
    if (!protocol_cfg_.artnet_enabled) return;
    DmxSourceId src;
    src.protocol = DmxSourceProtocol::ArtNet;
    src.identity[0] = static_cast<uint8_t>((packet.source_ip >> 24) & 0xFF);
    src.identity[1] = static_cast<uint8_t>((packet.source_ip >> 16) & 0xFF);
    src.identity[2] = static_cast<uint8_t>((packet.source_ip >> 8) & 0xFF);
    src.identity[3] = static_cast<uint8_t>(packet.source_ip & 0xFF);
    process_frame(packet.universe, packet.data, packet.length, 100,
                  src, packet.sequence);
}

void DmxRouter::handle_sacn_dmx(const SacnDmxPacket& packet) {
    if (!protocol_cfg_.sacn_enabled) return;
    DmxSourceId src;
    src.protocol = DmxSourceProtocol::Sacn;
    src.identity = packet.cid;
    process_frame(packet.universe, packet.data, packet.length, packet.priority,
                  src, packet.sequence);
}

DmxRouter::UniverseState* DmxRouter::get_or_create_universe(uint16_t universe) {
    const uint32_t current_ms = now_ms();
    const uint32_t stream_timeout = stream_timeout_ms();

    UniverseState* oldest_slot = nullptr;
    uint32_t oldest_age = 0xFFFFFFFF;

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
        oldest_slot->last_packet_ms = current_ms;
        oldest_slot->merged_data.fill(0);
        oldest_slot->merged_length = 0;
        for (auto& src : oldest_slot->sources) {
            src.active = false;
            src.sequence_valid = false;
        }
        oldest_slot->active = true;
        return oldest_slot;
    }

    return nullptr;
}

void DmxRouter::recompute_merge(UniverseState& state, uint32_t current_ms) {
    const uint32_t stream_timeout = stream_timeout_ms();

    // Prune timed-out sources
    uint8_t highest_priority = 0;
    bool any_active = false;

    for (auto& src : state.sources) {
        if (src.active) {
            if (current_ms - src.last_packet_ms > stream_timeout) {
                src.active = false;
                src.sequence_valid = false;
            } else {
                any_active = true;
                if (src.priority > highest_priority) {
                    highest_priority = src.priority;
                }
            }
        }
    }

    if (!any_active) {
        state.merged_data.fill(0);
        state.merged_length = 0;
        state.active = false;
        return;
    }

    state.merged_data.fill(0);
    uint16_t max_len = 0;

    if (protocol_cfg_.merge_mode == MergeMode::HTP) {
        // True HTP: maximum across all active sources sharing the highest priority
        for (const auto& src : state.sources) {
            if (src.active && src.priority == highest_priority) {
                max_len = std::max(max_len, src.length);
                for (uint16_t ch = 0; ch < src.length; ++ch) {
                    state.merged_data[ch] = std::max(state.merged_data[ch], src.data[ch]);
                }
            }
        }
    } else {
        // True LTP: newest packet timestamp among active sources sharing highest priority
        const SourceStream* latest_src = nullptr;
        uint32_t latest_ts = 0;

        for (const auto& src : state.sources) {
            if (src.active && src.priority == highest_priority) {
                if (!latest_src || src.last_packet_ms >= latest_ts) {
                    latest_src = &src;
                    latest_ts = src.last_packet_ms;
                }
            }
        }

        if (latest_src) {
            max_len = latest_src->length;
            std::memcpy(state.merged_data.data(), latest_src->data.data(), max_len);
        }
    }

    state.merged_length = max_len;
}

void DmxRouter::process_frame(uint16_t universe, const uint8_t* data, uint16_t length,
                              uint8_t priority, DmxSourceId source_id, uint8_t sequence) {
    if (!data || length == 0) return;
    if (length > 512) length = 512;

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

    std::lock_guard<std::mutex> lock(frame_mutex_);

    const uint32_t current_ms = now_ms();
    UniverseState* state = get_or_create_universe(universe);
    if (!state) return;

    const uint32_t timeout = stream_timeout_ms();

    // 1. Locate existing active source or allocate a free/timed-out slot
    SourceStream* target_src = nullptr;

    for (auto& src : state->sources) {
        if (src.active && src.source_id == source_id) {
            target_src = &src;
            break;
        }
    }

    if (!target_src) {
        for (auto& src : state->sources) {
            if (!src.active || (current_ms - src.last_packet_ms > timeout)) {
                target_src = &src;
                target_src->active = false;
                target_src->sequence_valid = false;
                break;
            }
        }
    }

    // If bounded capacity exceeded, drop newest source and increment counter; never overwrite active source
    if (!target_src) {
        dropped_sources_count_++;
        return;
    }

    // 2. Sequence handling MUST happen before merge state mutation (Batch 3 Item 7)
    if (target_src->active && target_src->sequence_valid) {
        if (source_id.protocol == DmxSourceProtocol::ArtNet && sequence == 0) {
            // Sequence 0 in Art-Net means unsequenced/disabled, accept without sequence check
        } else {
            if (!is_sequence_newer(sequence, target_src->last_sequence)) {
                return; // Stale or duplicate packet rejected
            }
        }
    }

    target_src->last_sequence = sequence;
    target_src->sequence_valid = true;
    target_src->source_id = source_id;
    target_src->priority = priority;
    target_src->last_packet_ms = current_ms;
    target_src->length = length;
    std::memcpy(target_src->data.data(), data, length);
    target_src->active = true;

    state->last_packet_ms = current_ms;
    recompute_merge(*state, current_ms);

    // Dispatch patched output
    dispatch_patch(universe, state->merged_data.data(), state->merged_length);
}

void DmxRouter::dispatch_patch(uint16_t universe, const uint8_t* data, uint16_t length) {
    const uint8_t live_layer = protocol_cfg_.live_stream_layer < 3 ? protocol_cfg_.live_stream_layer : 2;
    const uint32_t now = now_ms();

    // 1. Zone Wash Dispatch
    if (universe == protocol_cfg_.universe_start) {
        const uint16_t base_ch = (protocol_cfg_.zone_start_channel > 0) ? (protocol_cfg_.zone_start_channel - 1) : 0;
        for (uint8_t zone = 0; zone < 8; ++zone) {
            const size_t offset = base_ch + (zone * 3);
            if (offset + 2 >= length) break;

            auto& layer = live_frame_.layers[zone][live_layer];
            layer.color = BrandColor{
                data[offset + 0],
                data[offset + 1],
                data[offset + 2]
            };
            layer.opacity = 255;
            layer.timestamp_ms = now;
            layer.active = true;
        }
    }

    // 2. Direct Fixture Patch Dispatch
    for (uint8_t f = 0; f < 16; ++f) {
        const auto& fix = fixtures_[f];
        if (fix.dmx_start_channel == 0) continue; // Unpatched: inherits zone wash
        if (fix.dmx_universe != universe) continue;

        const size_t ch_offset = fix.dmx_start_channel - 1; // 0-indexed
        if (ch_offset >= length) continue;

        auto& lf = live_frame_.fixtures[f];
        const uint16_t pcount = std::min<uint16_t>(fix.num_pixels, MAX_PIXELS);
        lf.pixel_count = pcount;
        lf.timestamp_ms = now;
        lf.opacity = 255;
        lf.active = true;

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

            for (uint16_t p = 0; p < pcount; ++p) {
                lf.pixels[p] = lv;
            }
            for (uint8_t ch = 0; ch < 4; ++ch) {
                lf.pwm[ch] = pwm_duties[ch];
            }
        } else if (fix.dmx_mode == 1) {
            // Mode 1: Direct Pixel Map
            const uint8_t ch_per_pixel = (fix.profile == FixtureProfile::SK6812 || fix.profile == FixtureProfile::PWM_RGBW) ? 4 : 3;

            for (uint16_t p = 0; p < pcount; ++p) {
                size_t px_ch = ch_offset + (p * ch_per_pixel);
                if (px_ch + ch_per_pixel - 1 < length) {
                    if (ch_per_pixel == 4) {
                        lf.pixels[p] = {data[px_ch], data[px_ch + 1], data[px_ch + 2], data[px_ch + 3]};
                    } else {
                        lf.pixels[p] = {data[px_ch], data[px_ch + 1], data[px_ch + 2], 0};
                    }
                } else {
                    lf.pixels[p] = {0, 0, 0, 0};
                }
            }
            lf.pwm.fill(0);
        }
    }

    // 3. Single authoritative DMX telemetry buffer & atomic submission to LightingRuntime
    const uint16_t copy_len = std::min<uint16_t>(length, 512);
    std::memcpy(live_frame_.dmx_telemetry.data(), data, copy_len);
    live_frame_.dmx_telemetry_len = copy_len;
    live_frame_.timestamp_ms = now;
    live_frame_.active = true;

    lighting_.submit_live_frame(live_frame_);
}

} // namespace lg

