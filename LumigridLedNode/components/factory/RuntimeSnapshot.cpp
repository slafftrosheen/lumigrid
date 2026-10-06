// components/factory/RuntimeSnapshot.cpp
#include "RuntimeSnapshot.hpp"
#include "FactoryApi.hpp"
#include "FactoryEffects.hpp"
#include "RuntimeMemory.hpp"
#include "RuntimeServices.hpp"
#include "LightingRuntime.hpp"
#include "OutputController.hpp"
#include "PowerBudget.hpp"
#include "ConfigStore.hpp"
#include "Scheduler.hpp"
#include "NodeSync.hpp"
#include "DmxRouter.hpp"
#include "CueRunner.hpp"
#include "ShowTransport.hpp"
#include "WifiService.hpp"
#include "CertificationService.hpp"
#include "ChannelConfiguration.hpp"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdarg>
#include <cinttypes>
#include <cstdio>
#include <algorithm>

namespace lg {

static const char* TAG = "RuntimeSnapshot";

// ── FixedJsonWriter Implementation ───────────────────────────────────────────

bool FixedJsonWriter::append(const char* str) {
    if (!ok_ || !str) return false;
    size_t slen = strlen(str);
    if (len_ + slen + 1 > cap_) {
        ok_ = false;
        return false;
    }
    memcpy(buf_ + len_, str, slen);
    len_ += slen;
    buf_[len_] = '\0';
    return true;
}

bool FixedJsonWriter::append_printf(const char* fmt, ...) {
    if (!ok_ || !fmt) return false;
    if (len_ + 1 >= cap_) {
        ok_ = false;
        return false;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf_ + len_, cap_ - len_, fmt, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= (cap_ - len_)) {
        ok_ = false;
        buf_[cap_ - 1] = '\0';
        return false;
    }
    len_ += static_cast<size_t>(written);
    return true;
}

// ── Helper string converters ──────────────────────────────────────────────────

static const char* control_source_to_str(ControlSource src) {
    switch (src) {
        case ControlSource::EStop: return "EStop";
        case ControlSource::Factory: return "Factory";
        case ControlSource::LiveDmx: return "LiveDmx";
        case ControlSource::ManualOverride: return "ManualOverride";
        case ControlSource::Scheduler: return "Scheduler";
        case ControlSource::ScenePlayback: return "ScenePlayback";
        case ControlSource::Fallback: return "Fallback";
        case ControlSource::Idle: return "Idle";
        default: return "Unknown";
    }
}

static const char* output_state_to_str(LightingRuntime::OutputState state) {
    switch (state) {
        case LightingRuntime::OutputState::Armed: return "armed";
        case LightingRuntime::OutputState::Faulted: return "fault";
        case LightingRuntime::OutputState::Healthy:
        default:
            return "healthy";
    }
}

static const char* show_transport_mode_to_str(ShowTransportMode mode) {
    switch (mode) {
        case ShowTransportMode::Timeline: return "timeline";
        case ShowTransportMode::Cue: return "cue";
        case ShowTransportMode::Scene:
        default:
            return "scene";
    }
}

static const char* cue_state_to_str(uint8_t state) {
    switch (state) {
        case 1: return "holding";
        case 2: return "fading";
        case 3: return "paused";
        default: return "stopped";
    }
}

static const char* node_role_to_str(NodeRole role) {
    switch (role) {
        case NodeRole::Auto:   return "auto";
        case NodeRole::Master: return "master";
        case NodeRole::Worker: return "worker";
        default: return "auto";
    }
}

// ── Pure Observational Snapshot Collection ────────────────────────────────────

RuntimeSnapshot collect_runtime_snapshot(FactoryApi* api, uint32_t stream_seq) {
    RuntimeSnapshot snap{};
    snap.schema = 3;
    snap.stream_seq = stream_seq;
    snap.generated_at_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    if (!api) return snap;

    uint32_t now = esp_log_timestamp();

    // 1. Node Identity
    const NodeIdentity& id = api->identity();
    snprintf(snap.node.id, sizeof(snap.node.id), "%s", id.id);

    // 2. Runtime State
    snprintf(snap.runtime.effect, sizeof(snap.runtime.effect), "%s",
             effect_name(api->runner().status().active_effect));
    snap.runtime.test_active = api->runner().status().test_active;
    snap.runtime.auto_loop = api->runner().status().auto_loop;

    if (api->signage()) {
        snap.runtime.brightness = api->signage()->current_brightness();
        snprintf(snap.runtime.control_owner, sizeof(snap.runtime.control_owner), "%s",
                 control_source_to_str(api->signage()->current_control_source()));
        snap.runtime.frame_count = api->signage()->frame_count();
    } else {
        snap.runtime.brightness = api->runner().status().brightness;
        snprintf(snap.runtime.control_owner, sizeof(snap.runtime.control_owner), "Factory");
        snap.runtime.frame_count = 0;
    }

    // 3. Output State
    if (api->signage()) {
        const auto& ctrl = api->signage()->controller();
        const auto m = ctrl.commit_metrics();
        const auto ftm = api->signage()->frame_timing_metrics();
        snap.output.enabled = ctrl.output_enabled();
        snap.output.power_mw = api->signage()->power_budget().current_power_mw();
        snprintf(snap.output.state, sizeof(snap.output.state), "%s",
                 output_state_to_str(api->signage()->output_state()));
        snap.output.fault = api->signage()->output_fault();
        snap.output.fault_count = api->signage()->output_fault_count();
        snap.output.estop_active = api->signage()->is_estop_active();

        snap.output.commit_us = m.last_total_us;
        snap.output.max_commit_us = m.max_total_us;
        snap.output.addressable_us = m.last_addressable_us;
        snap.output.pwm_us = m.last_pwm_us;
        snap.output.commit_failures = static_cast<uint32_t>(m.failed);

        snap.output.frame.loop_fps = ftm.loop_fps;
        snap.output.frame.committed_fps = ftm.committed_fps;
        snap.output.frame.deadline_misses = ftm.deadline_misses;
    } else {
        snap.output.enabled = api->runner().status().outputs_enabled;
        snap.output.power_mw = 0;
        snprintf(snap.output.state, sizeof(snap.output.state), "healthy");
        snap.output.fault = false;
        snap.output.fault_count = 0;
        snap.output.estop_active = false;

        snap.output.commit_us = 0;
        snap.output.max_commit_us = 0;
        snap.output.addressable_us = 0;
        snap.output.pwm_us = 0;
        snap.output.commit_failures = api->runner().status().commit_failures;

        snap.output.frame.loop_fps = 0.0f;
        snap.output.frame.committed_fps = 0.0f;
        snap.output.frame.deadline_misses = 0;
    }

    // 4. Scene State
    if (api->signage()) {
        snap.scene.active_preset = api->signage()->active_scene_index();
        snap.scene.requested_preset = api->signage()->requested_scene_index();
        snap.scene.requested_generation = api->signage()->requested_scene_generation();
        snap.scene.applied_generation = api->signage()->applied_scene_generation();
        snap.scene.apply_pending = api->signage()->scene_apply_pending();
        snap.scene.transitioning = api->signage()->is_transitioning();
        snap.scene.fallback_active = api->signage()->is_fallback_active();
    }

    // 5. Manual State
    if (api->signage()) {
        ManualControlStatus man_st = api->signage()->manual_control_status();
        snap.manual.active = man_st.active;
        snap.manual.channel_mask = man_st.channel_mask;
        snap.manual.lease_remaining_ms = man_st.lease_remaining_ms;
    }

    // 6. Transport State
    if (api->show_transport()) {
        auto st = api->show_transport()->snapshot();
        if (api->scheduler()) {
            uint64_t now_ms = api->show_clock_now_ms();
            auto sched_st = api->scheduler()->transport_snapshot(now_ms);
            if (st.mode == ShowTransportMode::Timeline || sched_st.mode == ShowTransportMode::Timeline) {
                st.transport_generation = sched_st.transport_generation;
                st.timeline_anchor_ms = sched_st.timeline_anchor_ms;
                st.timeline_active_checksum = sched_st.timeline_active_checksum;
                st.timeline_saved_checksum = sched_st.timeline_saved_checksum;
                st.timeline_elapsed_ms = sched_st.timeline_elapsed_ms;
                st.timeline_current_beat = sched_st.timeline_current_beat;
                st.timeline_content_stale = sched_st.timeline_content_stale;
                if (st.mode == ShowTransportMode::Timeline) {
                    st.bpm = sched_st.bpm;
                }
            }
        }
        snprintf(snap.transport.mode, sizeof(snap.transport.mode), "%s",
                 show_transport_mode_to_str(st.mode));
        snap.transport.generation = st.generation;
        snap.transport.scene_index = st.active_scene;

        snap.transport.timeline.running = st.timeline_running();
        snap.transport.timeline.bpm = st.bpm;
        snap.transport.timeline.generation = st.transport_generation;
        snap.transport.timeline.anchor_ms = st.timeline_anchor_ms;
        snap.transport.timeline.elapsed_ms = static_cast<uint32_t>(st.timeline_elapsed_ms);
        snap.transport.timeline.current_beat = st.timeline_current_beat;
        snap.transport.timeline.active_checksum = st.timeline_active_checksum;
        snap.transport.timeline.saved_checksum = st.timeline_saved_checksum;
        snap.transport.timeline.content_stale = st.timeline_content_stale;

        // LG-034 §23: CueRunner is the runtime authority for cue execution.
        // Read its live state via one locked snapshot instead of the possibly
        // stale ShowTransportCoordinator copy. Purely observational (§24).
        if (api->cue_runner()) {
            const auto cue = api->cue_runner()->snapshot(now);
            snap.transport.cue.running = cue.running;
            snprintf(snap.transport.cue.state, sizeof(snap.transport.cue.state), "%s",
                     cue_runner_state_name(cue.state));
            snprintf(snap.transport.cue.paused_from, sizeof(snap.transport.cue.paused_from), "%s",
                     cue.state == CueRunnerState::Paused ? cue_runner_state_name(cue.paused_from) : "stopped");
            snprintf(snap.transport.cue.mode, sizeof(snap.transport.cue.mode), "%s",
                     cue_run_mode_name(cue.mode));
            snap.transport.cue.list_index = cue.list_index;
            snap.transport.cue.cue_index = cue.current_cue;
            snap.transport.cue.next_cue_index = cue.next_cue;
            snap.transport.cue.current_scene_index = cue.current_scene;
            snap.transport.cue.next_scene_index = cue.next_scene;
            snap.transport.cue.elapsed_ms = cue.elapsed_ms;
            snap.transport.cue.remaining_ms = cue.remaining_ms;
            snap.transport.cue.manual_fade = cue.manual_fade_q15;
            snap.transport.cue.transition_progress = cue.transition_progress_q15;
            snap.transport.cue.generation = cue.generation;
            snap.transport.cue.active_checksum = cue.active_checksum;
            snap.transport.cue.saved_checksum = cue.saved_checksum;
            snap.transport.cue.content_stale = cue.content_stale;
            snap.transport.cue.remote_driven = cue.remote_driven;
        } else if (st.cue.active) {
            snap.transport.cue.running = true;
            snprintf(snap.transport.cue.state, sizeof(snap.transport.cue.state), "%s",
                     cue_state_to_str(st.cue.state));
            snap.transport.cue.list_index = st.cue.list_index;
            snap.transport.cue.cue_index = st.cue.current_cue;
            snap.transport.cue.next_cue_index = st.cue.next_cue;
            snap.transport.cue.manual_fade = st.cue.manual_fade_q15;
        }
    } else {
        snprintf(snap.transport.mode, sizeof(snap.transport.mode), "scene");
        snap.transport.generation = 0;
        snap.transport.scene_index = snap.scene.active_preset;
        snap.transport.timeline.running = false;
        snap.transport.timeline.bpm = 120;
        snap.transport.cue.running = false;
        snprintf(snap.transport.cue.state, sizeof(snap.transport.cue.state), "stopped");
    }

    // 7. Sync State
    if (api->node_sync()) {
        const auto& sync_st = api->node_sync()->status();
        snprintf(snap.sync.configured_role, sizeof(snap.sync.configured_role), "%s",
                 node_role_to_str(sync_st.configured_role));
        snprintf(snap.sync.active_role, sizeof(snap.sync.active_role), "%s",
                 node_role_to_str(sync_st.active_role));
        snap.sync.clock_locked = sync_st.clock_locked;
        if (sync_st.master_address != 0) {
            uint32_t ip = sync_st.master_address;
            snprintf(snap.sync.master_address, sizeof(snap.sync.master_address),
                     "%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32,
                     ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        }
        snap.sync.last_packet_age_ms = sync_st.last_packet_age_ms;
        snap.sync.clock_offset_ms = static_cast<float>(sync_st.clock_offset_ms);
        snap.sync.clock_jitter_us = sync_st.clock_jitter_us;
    } else {
        snprintf(snap.sync.configured_role, sizeof(snap.sync.configured_role), "auto");
        snprintf(snap.sync.active_role, sizeof(snap.sync.active_role), "auto");
        snap.sync.clock_locked = false;
    }

    // 8. Network State
    wifi_ap_record_t ap_info{};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        snap.network.rssi = ap_info.rssi;
        snap.network.sta_connected = true;
        snprintf(snap.network.state, sizeof(snap.network.state), "connected");
    } else {
        snap.network.rssi = 0;
        snap.network.sta_connected = false;
        snprintf(snap.network.state, sizeof(snap.network.state), "disconnected");
    }

    if (api->wifi()) {
        snap.network.ap_active = api->wifi()->is_fallback_ap();
        if (api->wifi()->is_fallback_ap()) {
            snprintf(snap.network.state, sizeof(snap.network.state), "ap_only");
        }
    }

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, snap.network.sta_ip, sizeof(snap.network.sta_ip));
        }
    }

    NetworkApplyState apply_st = NetworkApplyState::Idle;
    uint32_t apply_cmd_id = 0;
    const char* apply_err = nullptr;
    if (api->network_apply_status_func()) {
        api->network_apply_status_func()(apply_st, apply_cmd_id, apply_err);
    }
    snprintf(snap.network.apply_state, sizeof(snap.network.apply_state), "%s",
             apply_st == NetworkApplyState::Applying ? "applying" :
             apply_st == NetworkApplyState::Failed ? "failed" : "idle");

    // 9. Protocols
    if (api->dmx_router()) {
        const auto stats = api->dmx_router()->get_stats();
        snap.protocols.artnet.enabled = true;
        snap.protocols.artnet.sources = stats.active_sources;
        snap.protocols.artnet.packets_per_second = stats.packets_accepted;
        snap.protocols.artnet.last_packet_age_ms = 0;

        snap.protocols.sacn.enabled = true;
        snap.protocols.sacn.sources = stats.active_sources;
        snap.protocols.sacn.packets_per_second = stats.packets_accepted;
        snap.protocols.sacn.last_packet_age_ms = 0;
    } else {
        snap.protocols.artnet.enabled = true;
        snap.protocols.sacn.enabled = true;
    }

    // 10. Timeline
    if (api->scheduler()) {
        snap.timeline.clock_ms = api->scheduler()->current_clock_ms();
        snap.timeline.drift_ppm = api->scheduler()->estimated_drift_ppm();
        snap.timeline.synced = api->scheduler()->is_time_synced();
    }

    // 11. Automation
    if (api->signage()) {
        snprintf(snap.automation.transition_curve, sizeof(snap.automation.transition_curve), "%s",
                 transition_curve_name(api->signage()->transition_curve()));
        snap.automation.active_tracks = api->signage()->scene_manager().current_motion().track_count;
    } else {
        snprintf(snap.automation.transition_curve, sizeof(snap.automation.transition_curve), "easeInOut");
        snap.automation.active_tracks = 0;
    }

    // 12. Job
    OperationJob cur_job;
    if (api->job_manager().get_current(cur_job)) {
        snap.job.has_job = true;
        snap.job.job_id = cur_job.job_id;
        snprintf(snap.job.type, sizeof(snap.job.type), "%s", job_type_name(cur_job.type));
        snprintf(snap.job.state, sizeof(snap.job.state), "%s", job_state_name(cur_job.state));
        snap.job.progress = cur_job.progress;
        snap.job.started_ms = cur_job.started_ms;
        snap.job.failed = (cur_job.error_code != 0);
    } else {
        snap.job.has_job = false;
    }

    // 13. Certification
    if (api->certification_service()) {
        auto cs = api->certification_service()->get_status_summary();
        snprintf(snap.certification.state, sizeof(snap.certification.state), "%s", certification_state_to_string(cs.state));
        snprintf(snap.certification.current_test, sizeof(snap.certification.current_test), "%s", cs.current_test);
        snap.certification.passed = cs.passed;
        snap.certification.failed = cs.failed;
        snap.certification.total = cs.total;
    } else {
        snprintf(snap.certification.state, sizeof(snap.certification.state), "not_started");
    }

    // 14. Memory (LG-038.3 §3/§57) + active services (§60)
    {
        const RuntimeMemorySnapshot mem = capture_runtime_memory();
        snap.memory.free8 = mem.free_8bit;
        snap.memory.largest8 = mem.largest_8bit;
        snap.memory.minimum_free = mem.minimum_free_heap;
        snap.memory.free_dma = mem.free_dma;

        const RuntimeMemoryCounters& counters = runtime_memory_counters();
        snap.memory_failures.api_allocation_failures = counters.api_allocation_failures;
        snap.memory_failures.preview_allocation_failures = counters.preview_allocation_failures;
        snap.memory_failures.rmt_allocation_failures = counters.rmt_allocation_failures;
        snap.memory_failures.factory_task_start_failures = counters.factory_task_start_failures;
        snap.memory_failures.network_task_start_failures = counters.network_task_start_failures;

        const ServiceStateTruth services = api->service_state_truth();
        snap.services.lighting = services.lighting;
        snap.services.scheduler = services.scheduler;
        snap.services.api = services.api;
        snap.services.mdns = services.mdns;
        snap.services.fleet_discovery = services.fleet_discovery;
        snap.services.artnet = services.artnet;
        snap.services.sacn = services.sacn;
        snap.services.node_sync = services.node_sync;
        snap.services.mqtt = services.mqtt;
        snap.services.factory_worker = services.factory_worker;
        snap.services.network_apply_worker = services.network_apply_worker;
    }

    // 15. Channels
    DeviceConfig status_cfg{};
    if (api->config()) {
        api->config()->load_config(status_cfg);
    }
    ControlSnapshot ctrl_snapshot = api->runner().control_sources().get_snapshot(now);

    for (uint8_t i = 0; i < kChannelCount; ++i) {
        auto& ch = snap.channels[i];
        ch.index = i;
        channel_port_name(i, ch.port, sizeof(ch.port));
        auto hw = channel_hardware(i);
        const auto& ch_cfg = status_cfg.channels[i];
        ch.configured = channel_is_configured(i, ch_cfg);
        ch.enabled = ch_cfg.enabled;
        ControlSource ch_source = ctrl_snapshot.resolve_channel(i, now);
        snprintf(ch.owner, sizeof(ch.owner), "%s", control_source_to_str(ch_source));
        ch.fault = snap.output.fault;

        if (hw.kind == ChannelKind::Addressable) {
            ch.is_addressable = true;
            ch.brightness = (snap.runtime.brightness * ch_cfg.max_brightness) / 255;
            ch.pixels = ch_cfg.pixel_count;
            ch.level = 0;
        } else {
            ch.is_addressable = false;
            ch.brightness = 0;
            ch.pixels = 0;
            ch.level = api->signage() ? api->signage()->last_pwm_level(i) : 0;
        }
    }

    return snap;
}

// ── Bounded JSON Serialization ────────────────────────────────────────────────

bool serialize_runtime_snapshot(const RuntimeSnapshot& snap, FixedJsonWriter& writer) {
    if (!writer.ok()) return false;

    // Open root & metadata
    writer.append("{\"schema\":3,");
    writer.append_printf("\"streamSeq\":%" PRIu32 ",\"generatedAtMs\":%" PRIu32 ",",
                         snap.stream_seq, snap.generated_at_ms);

    // Node
    writer.append_printf("\"node\":{\"id\":\"%s\"},", snap.node.id);

    // Runtime
    writer.append_printf(
        "\"runtime\":{\"effect\":\"%s\",\"brightness\":%u,\"testActive\":%s,\"autoLoop\":%s,\"controlOwner\":\"%s\",\"frameCount\":%" PRIu32 "},",
        snap.runtime.effect, snap.runtime.brightness,
        snap.runtime.test_active ? "true" : "false",
        snap.runtime.auto_loop ? "true" : "false",
        snap.runtime.control_owner, snap.runtime.frame_count
    );

    // Output
    writer.append_printf(
        "\"output\":{\"enabled\":%s,\"powerMw\":%" PRIu32 ",\"state\":\"%s\",\"fault\":%s,\"faultCount\":%" PRIu32 ",\"estopActive\":%s,"
        "\"commitUs\":%" PRIu32 ",\"maxCommitUs\":%" PRIu32 ",\"addressableUs\":%" PRIu32 ",\"pwmUs\":%" PRIu32 ",\"commitFailures\":%" PRIu32 ","
        "\"frame\":{\"loopFps\":%.1f,\"committedFps\":%.1f,\"deadlineMisses\":%" PRIu32 "}},",
        snap.output.enabled ? "true" : "false",
        snap.output.power_mw,
        snap.output.state,
        snap.output.fault ? "true" : "false",
        snap.output.fault_count,
        snap.output.estop_active ? "true" : "false",
        snap.output.commit_us,
        snap.output.max_commit_us,
        snap.output.addressable_us,
        snap.output.pwm_us,
        snap.output.commit_failures,
        snap.output.frame.loop_fps,
        snap.output.frame.committed_fps,
        snap.output.frame.deadline_misses
    );

    // Scene
    writer.append_printf(
        "\"scene\":{\"activePreset\":%u,\"requestedPreset\":%u,\"requestedGeneration\":%" PRIu32 ",\"appliedGeneration\":%" PRIu32 ",\"applyPending\":%s,\"transitioning\":%s,\"fallbackActive\":%s},",
        snap.scene.active_preset, snap.scene.requested_preset,
        snap.scene.requested_generation, snap.scene.applied_generation,
        snap.scene.apply_pending ? "true" : "false",
        snap.scene.transitioning ? "true" : "false",
        snap.scene.fallback_active ? "true" : "false"
    );

    // Manual
    writer.append_printf(
        "\"manual\":{\"active\":%s,\"channelMask\":%" PRIu32 ",\"leaseRemainingMs\":%" PRIu32 "},",
        snap.manual.active ? "true" : "false",
        snap.manual.channel_mask,
        snap.manual.lease_remaining_ms
    );

    // Transport
    writer.append_printf(
        "\"transport\":{\"mode\":\"%s\",\"generation\":%" PRIu32 ",\"sceneIndex\":%u,"
        "\"timeline\":{\"running\":%s,\"bpm\":%u,\"generation\":%" PRIu32 ",\"anchorMs\":%" PRIu64 ",\"elapsedMs\":%" PRIu32 ",\"currentBeat\":%" PRIu32 ",\"activeChecksum\":%" PRIu32 ",\"savedChecksum\":%" PRIu32 ",\"contentStale\":%s},"
        "\"cue\":{\"running\":%s",
        snap.transport.mode, snap.transport.generation, snap.transport.scene_index,
        snap.transport.timeline.running ? "true" : "false", snap.transport.timeline.bpm,
        snap.transport.timeline.generation, snap.transport.timeline.anchor_ms,
        snap.transport.timeline.elapsed_ms, snap.transport.timeline.current_beat,
        snap.transport.timeline.active_checksum, snap.transport.timeline.saved_checksum,
        snap.transport.timeline.content_stale ? "true" : "false",
        snap.transport.cue.running ? "true" : "false"
    );
    if (snap.transport.cue.running) {
        // LG-034 §22: full cue runtime telemetry (phase, timing, checksums,
        // scene indices, remote-driven flag).
        writer.append_printf(
            ",\"state\":\"%s\",\"pausedFrom\":\"%s\",\"listIndex\":%u,\"mode\":\"%s\","
            "\"cueIndex\":%u,\"nextCueIndex\":%u,\"currentSceneIndex\":%u,\"nextSceneIndex\":%u,"
            "\"elapsedMs\":%" PRIu32 ",\"remainingMs\":%" PRIu32 ","
            "\"manualFade\":%u,\"transitionProgress\":%u,\"generation\":%" PRIu32 ","
            "\"activeChecksum\":%" PRIu32 ",\"savedChecksum\":%" PRIu32 ",\"contentStale\":%s,\"remoteDriven\":%s",
            snap.transport.cue.state, snap.transport.cue.paused_from,
            snap.transport.cue.list_index, snap.transport.cue.mode,
            snap.transport.cue.cue_index, snap.transport.cue.next_cue_index,
            snap.transport.cue.current_scene_index, snap.transport.cue.next_scene_index,
            snap.transport.cue.elapsed_ms, snap.transport.cue.remaining_ms,
            snap.transport.cue.manual_fade, snap.transport.cue.transition_progress,
            snap.transport.cue.generation,
            snap.transport.cue.active_checksum, snap.transport.cue.saved_checksum,
            snap.transport.cue.content_stale ? "true" : "false",
            snap.transport.cue.remote_driven ? "true" : "false"
        );
    }
    writer.append("}},");

    // Sync
    writer.append_printf(
        "\"sync\":{\"configuredRole\":\"%s\",\"activeRole\":\"%s\",\"clockLocked\":%s,\"masterAddress\":\"%s\",\"lastPacketAgeMs\":%" PRIu32 ",\"clockOffsetMs\":%.2f,\"clockJitterUs\":%" PRIu32 "},",
        snap.sync.configured_role, snap.sync.active_role,
        snap.sync.clock_locked ? "true" : "false",
        snap.sync.master_address, snap.sync.last_packet_age_ms,
        snap.sync.clock_offset_ms, snap.sync.clock_jitter_us
    );

    // Network
    writer.append_printf(
        "\"network\":{\"state\":\"%s\",\"rssi\":%d,\"staConnected\":%s,\"staIp\":\"%s\",\"apActive\":%s,\"applyState\":\"%s\"},",
        snap.network.state, snap.network.rssi,
        snap.network.sta_connected ? "true" : "false",
        snap.network.sta_ip,
        snap.network.ap_active ? "true" : "false",
        snap.network.apply_state
    );

    // Protocols
    writer.append_printf(
        "\"protocols\":{\"artnet\":{\"enabled\":%s,\"sources\":%" PRIu32 ",\"packetsPerSecond\":%" PRIu32 ",\"lastPacketAgeMs\":%" PRIu32 "},"
        "\"sacn\":{\"enabled\":%s,\"sources\":%" PRIu32 ",\"packetsPerSecond\":%" PRIu32 "}},",
        snap.protocols.artnet.enabled ? "true" : "false",
        snap.protocols.artnet.sources, snap.protocols.artnet.packets_per_second, snap.protocols.artnet.last_packet_age_ms,
        snap.protocols.sacn.enabled ? "true" : "false",
        snap.protocols.sacn.sources, snap.protocols.sacn.packets_per_second
    );

    // Timeline
    writer.append_printf(
        "\"timeline\":{\"clockMs\":%" PRIu64 ",\"driftPpm\":%" PRId32 ",\"synced\":%s},",
        snap.timeline.clock_ms, snap.timeline.drift_ppm,
        snap.timeline.synced ? "true" : "false"
    );

    // Automation
    writer.append_printf(
        "\"automation\":{\"activeTracks\":%u,\"transitionCurve\":\"%s\"},",
        snap.automation.active_tracks, snap.automation.transition_curve
    );

    // Job
    if (snap.job.has_job) {
        writer.append_printf(
            "\"job\":{\"jobId\":%" PRIu32 ",\"type\":\"%s\",\"state\":\"%s\",\"progress\":%u,\"startedMs\":%" PRIu32 ",\"error\":%s},",
            snap.job.job_id, snap.job.type, snap.job.state, snap.job.progress, snap.job.started_ms,
            snap.job.failed ? "\"failed\"" : "null"
        );
    } else {
        writer.append("\"job\":null,");
    }

    // Certification (compact summary)
    writer.append_printf(
        "\"certification\":{\"state\":\"%s\",\"currentTest\":\"%s\",\"passed\":%u,\"failed\":%u,\"total\":%u},",
        snap.certification.state,
        snap.certification.current_test,
        snap.certification.passed,
        snap.certification.failed,
        snap.certification.total
    );

    // LG-038.3 §3: memory + §57 counters + §60 services (lightweight)
    writer.append_printf(
        "\"memory\":{\"free8\":%" PRIu32 ",\"largest8\":%" PRIu32 ",\"minimumFree\":%" PRIu32 ",\"freeDma\":%" PRIu32 "},",
        snap.memory.free8, snap.memory.largest8, snap.memory.minimum_free, snap.memory.free_dma
    );
    writer.append_printf(
        "\"memoryFailures\":{\"api\":%" PRIu32 ",\"preview\":%" PRIu32 ",\"rmt\":%" PRIu32 ",\"factoryTask\":%" PRIu32 ",\"networkTask\":%" PRIu32 "},",
        snap.memory_failures.api_allocation_failures,
        snap.memory_failures.preview_allocation_failures,
        snap.memory_failures.rmt_allocation_failures,
        snap.memory_failures.factory_task_start_failures,
        snap.memory_failures.network_task_start_failures
    );
    writer.append_printf(
        "\"services\":{\"lighting\":%s,\"factoryWorker\":%s,\"scheduler\":%s,\"api\":%s,\"mdns\":%s,\"fleetDiscovery\":%s,\"artnet\":%s,\"sacn\":%s,\"nodeSync\":%s,\"mqtt\":%s,\"networkApplyWorker\":%s},",
        snap.services.lighting ? "true" : "false",
        snap.services.factory_worker ? "true" : "false",
        snap.services.scheduler ? "true" : "false",
        snap.services.api ? "true" : "false",
        snap.services.mdns ? "true" : "false",
        snap.services.fleet_discovery ? "true" : "false",
        snap.services.artnet ? "true" : "false",
        snap.services.sacn ? "true" : "false",
        snap.services.node_sync ? "true" : "false",
        snap.services.mqtt ? "true" : "false",
        snap.services.network_apply_worker ? "true" : "false"
    );

    // Channels
    writer.append("\"channels\":[");
    for (uint8_t i = 0; i < kChannelCount; ++i) {
        const auto& ch = snap.channels[i];
        if (i > 0) writer.append(",");

        if (ch.is_addressable) {
            writer.append_printf(
                "{\"index\":%u,\"port\":\"%s\",\"configured\":%s,\"enabled\":%s,\"owner\":\"%s\",\"fault\":%s,\"brightness\":%u,\"pixels\":%u}",
                ch.index, ch.port,
                ch.configured ? "true" : "false",
                ch.enabled ? "true" : "false",
                ch.owner,
                ch.fault ? "true" : "false",
                ch.brightness, ch.pixels
            );
        } else {
            writer.append_printf(
                "{\"index\":%u,\"port\":\"%s\",\"configured\":%s,\"enabled\":%s,\"owner\":\"%s\",\"fault\":%s,\"level\":%u}",
                ch.index, ch.port,
                ch.configured ? "true" : "false",
                ch.enabled ? "true" : "false",
                ch.owner,
                ch.fault ? "true" : "false",
                ch.level
            );
        }
    }
    writer.append("]}");

    if (!writer.ok()) {
        ESP_LOGE(TAG, "RuntimeSnapshot JSON serialization overflowed buffer (len=%zu, cap=%zu)",
                 writer.size(), writer.capacity());
        return false;
    }

    return true;
}

} // namespace lg
