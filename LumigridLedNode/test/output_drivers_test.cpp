// Host tests of the production output path. No CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
// branches are compiled; only SDK boundaries and task dispatch are substituted.
#include "OutputController.hpp"
#include "OutputCoreExecutor.hpp"
#include "RuntimeMemory.hpp"
#include "BoardOutputSafety.hpp"
#include "lg_board_pins.hpp"
#include "driver/rmt_tx.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
#error These tests must execute the production controller path
#endif

// Scheduling is outside this host test: execute the same CPU1 lifecycle entry
// points synchronously. Mutexes, transactions and all controller code remain real.
namespace lg {
OutputCoreExecutor::OutputCoreExecutor() = default;
OutputCoreExecutor::~OutputCoreExecutor() = default;
OutputCoreExecutor& OutputCoreExecutor::instance() { static OutputCoreExecutor executor; return executor; }
esp_err_t OutputCoreExecutor::execute_init(OutputController* c) { return c->init_on_output_core(); }
esp_err_t OutputCoreExecutor::execute_reconfigure(OutputController* c, const DeviceConfig& cfg) { return c->reconfigure_on_output_core(cfg); }
esp_err_t OutputCoreExecutor::execute_pca_recovery(OutputController* c) { return c->recover_pca_on_output_core(); }
RuntimeMemoryCounters& runtime_memory_counters() { static RuntimeMemoryCounters counters; return counters; }
}

namespace {
using namespace lg;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_disabled(const char* reason) {
    require(!Pca9685Driver::is_outputs_enabled() && board::sample_pca_oe_level() == 1, reason);
}

struct PcaTransport final : IPcaTransport {
    bool live = false;
    esp_err_t init_error = ESP_OK;
    esp_err_t recovery_error = ESP_OK;
    unsigned fail_frames = 0;
    std::array<uint8_t, 256> registers{};
    std::array<uint16_t, Pca9685Driver::kChannels> duties{};
    unsigned frames = 0;
    std::function<void()> frame_hook;

    esp_err_t init_bus() override { live = init_error == ESP_OK; return init_error; }
    esp_err_t deinit_bus() override { live = false; return ESP_OK; }
    bool is_bus_initialized() const override { return live; }
    esp_err_t write(uint8_t address, const uint8_t* data, size_t length, uint32_t timeout) override {
        require(address == board::kPcaAddress && timeout == kPcaTransactionTimeoutMs, "wrong PCA transport contract");
        if (!live) return ESP_ERR_INVALID_STATE;
        require(length >= 2 && size_t(data[0]) + length - 1 <= registers.size(), "invalid PCA register packet");
        if (data[0] == 0x06 && fail_frames) { --fail_frames; return ESP_ERR_TIMEOUT; }
        std::copy(data + 1, data + length, registers.begin() + data[0]);
        if (data[0] == 0x06) {
            require(length == 1 + duties.size() * 4, "PWM frame omitted a channel");
            ++frames;
            for (size_t ch = 0; ch < duties.size(); ++ch) {
                const uint8_t* bytes = data + 1 + ch * 4;
                duties[ch] = bytes[3] & 0x10 ? 0 : bytes[1] & 0x10 ? 4095 :
                    uint16_t(bytes[2] | ((bytes[3] & 0x0f) << 8));
            }
        }
        if (data[0] == 0x06 && frame_hook) {
            auto hook = std::move(frame_hook);
            frame_hook = {};
            hook();
        }
        return ESP_OK;
    }
    esp_err_t write_read(uint8_t, const uint8_t* data, size_t write_length,
                         uint8_t* out, size_t read_length, uint32_t) override {
        if (!live) return ESP_ERR_INVALID_STATE;
        require(write_length == 1 && read_length == 1, "unexpected PCA register read");
        *out = registers[data[0]];
        return ESP_OK;
    }
    bool sda_high() const override { return true; }
    bool scl_high() const override { return true; }
    esp_err_t recover_bus(uint32_t& pulses, bool& released) override {
        pulses = 0;
        released = recovery_error == ESP_OK;
        if (recovery_error == ESP_OK) live = true;
        return recovery_error;
    }
};

struct RmtTransport final : IRmtTransport {
    struct Channel {
        bool live = false, pending = false;
        uint32_t completions = 0, symbols = 0;
        uint32_t submissions = 0, last_timeout_us = 0;
        const uint8_t* borrowed = nullptr;
        std::vector<uint8_t> frame;
    };
    struct Event { char kind; uint8_t lane; };
    std::array<Channel, MAX_RMT_LANES> channels{};
    std::vector<Event> events;
    int drop_completion_lane = -1;
    int wrong_symbols_lane = -1;
    int stall_lane = -1;
    unsigned destroy_calls = 0;

    esp_err_t create(uint8_t lane, int, bool) override {
        require(!channels[lane].live, "RMT create replaced a live channel");
        // The ISR ledger is owned by the physical channel. Production create()
        // and destroy() reset it; an encoder-only update retains it.
        channels[lane] = Channel{};
        channels[lane].live = true;
        return ESP_OK;
    }
    esp_err_t prepare(uint8_t lane, int gpio, bool rgbw, bool existing) override {
        require(channels[lane].live == existing, "candidate/channel ownership mismatch");
        return existing ? ESP_OK : create(lane, gpio, rgbw);
    }
    esp_err_t submit(uint8_t lane, const uint8_t* data, size_t length) override {
        auto& channel = channels[lane];
        require(channel.live && !channel.pending, "RMT submit without an idle live channel");
        channel.pending = true;
        channel.borrowed = data;
        ++channel.submissions;
        channel.symbols = uint32_t(length * 8 + 2) + (wrong_symbols_lane == lane ? 1 : 0);
        channel.frame.assign(data, data + length);
        events.push_back({'S', lane});
        return ESP_OK;
    }
    esp_err_t wait(uint8_t lane, uint32_t timeout) override {
        require(timeout > 0, "unbounded or zero RMT wait budget");
        auto& channel = channels[lane];
        require(channel.live, "RMT wait without a live channel");
        channel.last_timeout_us = timeout;
        events.push_back({'W', lane});
        if (channel.pending) {
            require(std::equal(channel.frame.begin(), channel.frame.end(), channel.borrowed), "SDK-owned RMT payload was mutated or freed");
            if (stall_lane == lane) return ESP_ERR_TIMEOUT;
            if (drop_completion_lane != lane) ++channel.completions;
            channel.pending = false;
            channel.borrowed = nullptr;
        }
        return ESP_OK;
    }
    esp_err_t destroy(uint8_t lane) override {
        require(!channels[lane].pending, "channel destroyed before its SDK payload was released");
        ++destroy_calls;
        channels[lane] = Channel{};
        return ESP_OK;
    }
    uint32_t get_completed_count(uint8_t lane) const override { return channels[lane].completions; }
    uint32_t get_last_num_symbols(uint8_t lane) const override { return channels[lane].symbols; }
};

DeviceConfig config(uint8_t rmt_mask = 0, uint8_t pwm_mask = 0, uint16_t pixels = 4) {
    DeviceConfig cfg{};
    for (uint8_t lane = 0; lane < 8; ++lane) {
        auto& ch = cfg.channels[lane];
        ch.enabled = (rmt_mask & (1u << lane)) != 0;
        ch.pixel_count = pixels;
        ch.addressable_type = lane & 1 ? AddressableType::SK6812 : AddressableType::WS2812;
        ch.pixel_order = lane & 1 ? PixelOrder::GRBW : PixelOrder::GRB;
        cfg.channels[8 + lane].enabled = (pwm_mask & (1u << lane)) != 0;
    }
    return cfg;
}

struct ControllerFixture {
    PcaTransport pca;
    RmtTransport rmt;
    OutputController controller;
    ControllerFixture() {
        Pca9685Driver::force_disable_outputs();
        controller.pwm().set_transport(&pca);
        require(controller.pixels().use_transport(&rmt) == ESP_OK, "RMT injection failed");
    }
    void start(const DeviceConfig& cfg) {
        require(controller.init() == ESP_OK, "controller init failed");
        require(controller.reconfigure(cfg) == ESP_OK, "controller topology failed");
    }
};

// Exercise the actual EspRmtTransport implementation, with fault injection only
// at the public SDK boundary. Cleanup also permits all red cases to run against
// the original implementation, which forgets handles after failed deletion.
struct ProductionRmtFixture {
    ProductionRmtFixture() {
        require(host_rmt::channels.empty() && host_rmt::live_encoders == 0, "previous SDK test leaked transport resources");
        host_rmt::active = true;
        host_rmt::clear_faults();
        host_rmt::creates = host_rmt::disables = host_rmt::deletes = 0;
        host_rmt::encoder_deletes = host_rmt::submissions = 0;
    }
    ~ProductionRmtFixture() {
        host_rmt::clear_faults();
        for (uint8_t lane = 0; lane < MAX_RMT_LANES; ++lane) production_rmt_transport().destroy(lane);
        // These can only be orphaned by the broken implementation in a red run.
        for (auto* channel : host_rmt::channels) delete channel;
        host_rmt::channels.clear();
        host_rmt::last_channel = nullptr;
        host_rmt::active = false;
    }
};

void pca_nonzero_enable() {
    PcaTransport transport;
    Pca9685Driver pca(&transport);
    require(pca.enable_outputs() == ESP_ERR_INVALID_STATE, "uninitialized PWM was exposed");
    require(pca.init() == ESP_OK && pca.zero_confirmed(), "startup zero not confirmed");
    require_disabled("PCA init exposed PWM before controller commit");
    require(std::all_of(transport.duties.begin(), transport.duties.end(), [](auto v) { return v == 0; }), "startup frame not zero");
    require(pca.set(0, 1234) == ESP_OK && pca.set(1, 4095) == ESP_OK, "PWM staging failed");
    require(pca.commit() == ESP_OK && !pca.zero_confirmed(), "nonzero frame status incorrect");
    require(transport.duties[0] == 1234 && transport.duties[1] == 4095, "12-bit/full-on PWM encoding incorrect");
    require_disabled("driver commit changed OE without controller authorization");
    require(pca.enable_outputs() == ESP_OK, "valid nonzero PWM frame cannot enable OE");
    require(Pca9685Driver::is_outputs_enabled() && board::sample_pca_oe_level() == 0, "PWM enable did not reach GPIO");
    pca.disable_outputs();
}

void pca_fault_latch() {
    PcaTransport transport;
    Pca9685Driver pca(&transport);
    require(pca.init() == ESP_OK, "PCA init failed");
    transport.fail_frames = 1;
    pca.set(0, 1000);
    require(pca.commit() == ESP_ERR_TIMEOUT && pca.recovery_required(), "PCA fault did not require recovery");
    require_disabled("PCA transport failure left OE active");
    require(pca.clear() == ESP_OK && pca.zero_confirmed(), "successful emergency zero failed");
    require(pca.enable_outputs() == ESP_ERR_INVALID_STATE, "zero write silently cleared the fault latch");
    require(pca.recover() == ESP_OK && !pca.recovery_required(), "explicit PCA recovery failed");
    require_disabled("PCA recovery exposed PWM");
    pca.set(0, 600);
    require(pca.commit() == ESP_OK && pca.enable_outputs() == ESP_OK, "PWM did not resume after verified recovery");
    pca.disable_outputs();
}

void pca_failed_recovery() {
    PcaTransport transport;
    Pca9685Driver pca(&transport);
    require(pca.init() == ESP_OK && pca.enable_outputs() == ESP_OK, "PCA init failed");
    transport.recovery_error = ESP_ERR_TIMEOUT;
    require(pca.recover() == ESP_ERR_TIMEOUT, "failed bus clear was hidden");
    require(pca.recovery_required() && !pca.zero_confirmed(), "failed recovery retained stale readiness");
    require_disabled("failed recovery exposed PWM");
    require(pca.clear() == ESP_OK, "transport zero failed after bus-clear failure");
    require(pca.enable_outputs() == ESP_ERR_INVALID_STATE, "failed recovery could be bypassed with a zero write");
    transport.recovery_error = ESP_OK;
    require(pca.recover() == ESP_OK && pca.zero_confirmed(), "second recovery failed");
    require_disabled("second recovery bypassed controller enable");
}

void rmt_recreated_lane() {
    RmtTransport transport;
    ParallelRmtDriver rmt(&transport);
    auto cfg = config(1);
    require(rmt.reconfigure(cfg, 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "initial RMT black failed");
    rmt.stage_pixel(0, 0, 50, 100, 150, 0);
    require(rmt.commit().result == ESP_OK, "initial RMT content failed");
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        cfg.channels[0].enabled = false;
        require(rmt.reconfigure(cfg, 0, 0) == ESP_OK && !rmt.configured(0), "RMT removal failed");
        require(!transport.channels[0].live && transport.channels[0].completions == 0, "removed channel retained its ISR ledger");
        cfg.channels[0].enabled = true;
        require(rmt.reconfigure(cfg, 0, 0) == ESP_OK, "RMT recreation failed");
        require(rmt.commit().result == ESP_OK, "recreated lane inherited a stale submission generation");
        require(transport.channels[0].completions == 1 && rmt.lane_state(0) == LaneState::Ready, "recreated channel ledger not fresh");
    }
    require(rmt.diagnostics().lanes[0].completion_mismatches == 0, "valid recreation generated a parity fault");
}

void rmt_retained_lane() {
    RmtTransport transport;
    ParallelRmtDriver rmt(&transport);
    auto cfg = config(1);
    require(rmt.reconfigure(cfg, 0, 0) == ESP_OK, "initial topology failed");
    for (unsigned frame = 0; frame < 4; ++frame) require(rmt.commit().result == ESP_OK, "initial black failed");
    cfg.channels[0].pixel_count = 19;
    cfg.channels[0].addressable_type = AddressableType::SK6812;
    cfg.channels[0].pixel_order = PixelOrder::RGBW;
    require(rmt.reconfigure(cfg, 0, 0) == ESP_OK, "encoder replacement failed");
    require(transport.channels[0].completions == 4, "encoder update reset a live channel ledger");
    require(rmt.commit().result == ESP_OK && transport.channels[0].completions == 5, "retained channel lost its submission generation");
    require(rmt.has_white(0) && transport.channels[0].frame.size() == 19 * 4, "replacement encoder frame shape incorrect");
}

void rmt_cancel_candidate() {
    RmtTransport transport;
    ParallelRmtDriver rmt(&transport);
    auto cfg = config(1);
    require(rmt.reconfigure(cfg, 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "initial black failed");
    rmt.stage_pixel(0, 0, 19, 27, 35, 0);
    auto next = config(3, 0, 25);
    require(rmt.prepare_candidate(next, 0, 0) == ESP_OK, "candidate preparation failed");
    rmt.cancel_candidate();
    require(rmt.pixels(0) == 4 && !rmt.configured(1) && !transport.channels[1].live, "cancel changed live topology");
    require(rmt.commit().result == ESP_OK && transport.channels[0].completions == 2, "cancel reset the retained generation");
    require(transport.channels[0].frame[0] == 27, "cancel replaced old staged content");
}

void rmt_completion_guards() {
    for (bool missing_callback : {true, false}) {
        RmtTransport transport;
        ParallelRmtDriver rmt(&transport);
        require(rmt.reconfigure(config(1), 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "initial black failed");
        if (missing_callback) transport.drop_completion_lane = 0;
        else transport.wrong_symbols_lane = 0;
        const auto result = rmt.commit();
        require(result.result == (missing_callback ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_SIZE), "RMT completion corruption was accepted");
        require(rmt.lane_state(0) == LaneState::Faulted, "bad completion did not fault its lane");
        const auto diag = rmt.diagnostics().lanes[0];
        require((missing_callback ? diag.completion_mismatches : diag.symbol_count_mismatches) == 1, "wrong completion diagnostic");
    }
}

void controller_mixed_output() {
    ControllerFixture f;
    f.start(config(0xff, 0xff));
    require_disabled("topology activation exposed PWM before a normal frame");
    f.rmt.events.clear();
    esp_err_t observed = ESP_FAIL;
    f.controller.set_commit_observer([&](esp_err_t error) { observed = error; });
    auto tx = f.controller.begin_transaction();
    for (uint8_t lane = 0; lane < 8; ++lane) {
        tx.pixels().fill(lane, 17 + lane, 40, 80, 11);
        tx.pwm().set(lane, 100 + lane * 300);
    }
    require(tx.commit() == ESP_OK && observed == ESP_OK, "8 RMT + 8 PWM commit failed");
    require(Pca9685Driver::is_outputs_enabled(), "successful controller commit left nonzero PWM disabled");
    require(f.rmt.events.size() == 16, "not every RMT lane was submitted and waited");
    for (uint8_t lane = 0; lane < 8; ++lane) {
        require(f.rmt.events[lane].kind == 'S' && f.rmt.events[lane + 8].kind == 'W', "RMT transmission was serialized");
        require(f.pca.duties[lane] == 100 + lane * 300, "mixed commit changed an unconstrained PWM duty");
    }
    const auto metrics = f.controller.commit_metrics();
    require(metrics.attempted == 1 && metrics.successful == 1 && metrics.failed == 0, "successful output metrics incorrect");
    f.controller.blackout();
    require_disabled("blackout left PWM enabled");
}

void controller_disabled_pwm() {
    ControllerFixture f;
    auto cfg = config(0, 1);
    cfg.max_power_mw = 1000;
    for (uint8_t ch = 8; ch < 16; ++ch) cfg.channels[ch].rated_full_power_mw = ch == 8 ? 1000 : 100000;
    f.start(cfg);
    f.controller.pwm().set_all(2048);
    require(f.controller.try_commit() == ESP_OK, "masked PWM commit failed");
    require(f.pca.duties[0] == 2048, "disabled channels consumed the active channel's power budget");
    for (uint8_t ch = 1; ch < 8; ++ch) require(f.pca.duties[ch] == 0, "disabled PWM channel reached hardware");
    // Power/config hot apply must affect the final physical gate immediately.
    cfg.channels[8].enabled = false;
    cfg.channels[15].enabled = true;
    cfg.channels[15].rated_full_power_mw = 1000;
    f.controller.set_power_config(cfg);
    f.controller.pwm().set_all(1000);
    require(f.controller.try_commit() == ESP_OK, "hot config PWM commit failed");
    for (uint8_t ch = 0; ch < 8; ++ch) require(f.pca.duties[ch] == (ch == 7 ? 1000 : 0), "hot disabled mask was not enforced");
    f.controller.blackout();
}

void controller_enable_failure() {
    ControllerFixture f;
    f.start(config(1, 1));
    f.pca.fail_frames = 1;
    f.controller.pwm().set(0, 1000);
    require(f.controller.try_commit() == ESP_ERR_TIMEOUT, "transport error was hidden");
    require(f.controller.pwm().zero_confirmed(), "failed commit did not write emergency zeros");
    require_disabled("failed commit did not disable PWM");
    esp_err_t observed = ESP_OK;
    f.controller.set_commit_observer([&](esp_err_t error) { observed = error; });
    // The write now succeeds, but enable must still reject the unrecovered fault.
    require(f.controller.try_commit() == ESP_ERR_INVALID_STATE, "controller reported success after output enable was refused");
    require(observed == ESP_ERR_INVALID_STATE && f.controller.last_commit_error() == ESP_ERR_INVALID_STATE, "enable failure was absent from status/observer");
    require(f.controller.last_fault_domain() == OutputFaultDomain::PcaI2c, "enable refusal lost its PWM fault domain");
    const auto metrics = f.controller.commit_metrics();
    require(metrics.attempted == 2 && metrics.failed == 2 && metrics.successful == 0, "enable refusal counted as a successful commit");
    require_disabled("refused enable did not remain safe");
    require(f.controller.recover_pca() == ESP_OK, "controller recovery failed");
    require_disabled("recovery enabled PWM before a normal frame");
    f.controller.pwm().set(0, 700);
    require(f.controller.try_commit() == ESP_OK && Pca9685Driver::is_outputs_enabled(), "verified recovery did not resume PWM");
    f.controller.blackout();
}

void controller_initial_recovery() {
    ControllerFixture f;
    f.pca.init_error = ESP_ERR_NOT_FOUND;
    f.start(config(1));
    require(!f.controller.is_pwm_available(), "failed initial PCA was available");
    require(f.controller.recover_pca() == ESP_OK, "initial PCA failure could not recover");
    require(f.controller.is_pwm_available(), "successful recovery did not publish PCA availability");
    require_disabled("initial-failure recovery exposed PWM");
    require(f.controller.reconfigure(config(1, 1)) == ESP_OK, "recovered PCA still rejected a PWM topology");
    f.controller.pwm().set(0, 500);
    require(f.controller.try_commit() == ESP_OK && f.pca.duties[0] == 500, "recovered PCA commit was skipped");
    f.controller.blackout();
}

void controller_recovery_preserves_estop() {
    ControllerFixture f;
    f.start(config(1, 1));
    f.controller.latch_estop();
    require(f.controller.recover_pca() == ESP_OK, "recovery under E-stop failed");
    require(f.controller.is_estop_latched() && !f.controller.output_enabled(), "PCA recovery cleared the emergency latch");
    require(f.controller.try_commit() == ESP_ERR_INVALID_STATE, "PCA recovery bypassed E-stop");
    require_disabled("recovery under E-stop enabled outputs");
    require(f.controller.clear_estop() == ESP_OK, "explicit E-stop clear failed");
    require_disabled("E-stop clear exposed PWM before a normal frame");
    f.controller.pwm().set(0, 300);
    require(f.controller.try_commit() == ESP_OK && Pca9685Driver::is_outputs_enabled(), "explicit E-stop clear could not resume");
    f.controller.blackout();
}

void controller_lane_toggle() {
    ControllerFixture f;
    f.start(config(0xff, 0xff));
    require(f.controller.try_commit() == ESP_OK, "initial mixed commit failed");
    require(f.controller.reconfigure(config(0x55, 0xff)) == ESP_OK, "partial lane removal failed");
    require(f.controller.reconfigure(config(0xff, 0xff, 13)) == ESP_OK, "recreated lanes failed controller blackout verification");
    require(f.controller.pixels().active_lanes() == 8 && f.controller.topology_generation() == 3, "mixed retained/recreated topology incorrect");
    require(f.controller.topology_rollbacks() == 0 && f.controller.topology_reconfigure_failures() == 0, "valid lane toggle triggered rollback");
    require_disabled("topology mutation exposed PWM before normal commit");
    require(f.controller.try_commit() == ESP_OK, "mixed retained/recreated lanes cannot commit");
    f.controller.blackout();
}

void rmt_lease_blocks_reconfigure() {
    RmtTransport transport;
    ParallelRmtDriver rmt(&transport);
    require(rmt.reconfigure(config(1), 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "initial black failed");
    require(rmt.prepare_candidate(config(3, 0, 13), 0, 0) == ESP_OK, "candidate prepare failed");
    require(rmt.stage_pixel(0, 0, 31, 47, 59, 0), "initial staging failed");
    transport.stall_lane = 0;
    require(rmt.commit().result == ESP_ERR_TIMEOUT, "stalled RMT did not time out");
    const uint8_t* held = rmt.wire_data(0);
    const auto expected = transport.channels[0].frame;
    require(rmt.wire_in_flight(0), "timeout released SDK ownership");
    require(!rmt.stage_pixel(0, 0, 200, 201, 202, 0), "borrowed pixel staging was accepted");
    require(!rmt.stage_clear_lane(0), "borrowed zero staging was accepted");
    rmt.scale_lane(0, 0);
    require(std::equal(expected.begin(), expected.end(), held), "staging changed borrowed data");
    require(rmt.commit_candidate() == ESP_ERR_INVALID_STATE, "candidate swapped a borrowed buffer/encoder");
    require(rmt.wire_data(0) == held && rmt.pixels(0) == 4, "failed swap changed live topology");
    rmt.cancel_candidate();
    require(!transport.channels[1].live, "canceled candidate leaked its new channel");
    const auto destroys = transport.destroy_calls;
    require(rmt.release_lane(0) == ESP_ERR_TIMEOUT, "release ignored an outstanding lease");
    require(rmt.configure_lane(0, board::kAledPins[0], false, PixelOrder::GRB, 8) == ESP_ERR_TIMEOUT, "direct configure replaced a borrowed buffer");
    require(rmt.reconfigure(config(1, 0, 9), 0, 0) == ESP_ERR_TIMEOUT, "candidate prepare ignored an outstanding lease");
    require(transport.destroy_calls == destroys && rmt.wire_data(0) == held, "failed drain freed or destroyed leased resources");
    rmt.clear_faults();
    require(rmt.commit().result == ESP_ERR_INVALID_STATE, "rejected staging was falsely acknowledged");
    require(transport.channels[0].submissions == 2, "fault retry queued another borrowed payload");
    transport.stall_lane = -1;
    require(rmt.commit(true).result == ESP_OK, "bounded drain and fresh zero recovery failed");
    require(!rmt.wire_in_flight(0) && rmt.lane_state(0) == LaneState::Ready, "recovered lane remained leased/faulted");
    require(std::all_of(transport.channels[0].frame.begin(), transport.channels[0].frame.end(), [](auto v) { return v == 0; }), "recovery retransmitted stale nonzero content");
    require(rmt.stage_pixel(0, 0, 11, 22, 33, 0) && rmt.commit().result == ESP_OK, "fresh content failed after zero recovery");
    require(rmt.diagnostics().lanes[0].completion_mismatches == 0, "drain recovery reset the completion generation");
}

void rmt_destructor_retains_lease() {
    RmtTransport transport;
    auto rmt = std::make_unique<ParallelRmtDriver>(&transport);
    require(rmt->reconfigure(config(1), 0, 0) == ESP_OK && rmt->commit().result == ESP_OK, "initial black failed");
    rmt->stage_pixel(0, 0, 13, 27, 39, 0);
    transport.stall_lane = 0;
    require(rmt->commit().result == ESP_ERR_TIMEOUT, "stalled RMT did not time out");
    const uint8_t* held = transport.channels[0].borrowed;
    rmt.reset();
    require(transport.channels[0].pending && transport.destroy_calls == 0, "destructor tore down a leased channel");
    // ASan checks that this still-borrowed pointer was retained, not freed.
    require(std::equal(transport.channels[0].frame.begin(), transport.channels[0].frame.end(), held), "destructor released or changed SDK-owned data");
    // The production fail-closed destructor intentionally retains this allocation.
    // Finish the fake transfer and explicitly clean it up in the host harness.
    transport.stall_lane = -1;
    require(transport.wait(0, addressable_timeout_us(4, false)) == ESP_OK, "fake transfer did not finish");
    require(transport.destroy(0) == ESP_OK, "fake channel cleanup failed");
    heap_caps_free(const_cast<uint8_t*>(held));
}

void controller_timeout_quarantine() {
    ControllerFixture f;
    f.start(config(0xff, 0xff));
    for (uint8_t lane = 0; lane < 8; ++lane) f.controller.pixels().fill(lane, 17, 31, 47, 9);
    f.controller.pwm().set_all(400);
    f.rmt.stall_lane = 0;
    require(f.controller.try_commit() == ESP_ERR_TIMEOUT, "stalled mixed commit was reported successful");
    require_disabled("addressable timeout left PWM exposed");
    const auto original = f.rmt.channels[0].frame;
    const auto submissions = f.rmt.channels[0].submissions;
    const auto* held = f.rmt.channels[0].borrowed;
    require(submissions == 2, "blackout queued behind the timed-out transfer");
    f.controller.pixels().fill(0, 200, 201, 202);
    f.controller.pixels().scale_lane(0, 0);
    f.controller.pixels().clear();
    require(f.controller.pixels().shadow_pixel(0, 0).r == 17, "diagnostic shadow claimed rejected staging");
    for (unsigned retry = 0; retry < 4; ++retry) {
        require(f.controller.try_commit() != ESP_OK, "stalled/rejected content was falsely acknowledged");
        require(f.rmt.channels[0].submissions == submissions && f.rmt.channels[0].pending, "stalled retry grew the RMT queue");
        require(std::equal(original.begin(), original.end(), held), "stalled retry changed SDK-owned data");
        require(!f.controller.last_blackout_result().addressable_confirmed, "unconfirmed blackout was reported dark");
    }
    require(f.rmt.channels[0].last_timeout_us == addressable_timeout_us(4, false), "timeout recovery changed the wire deadline");
    for (uint8_t lane = 1; lane < 8; ++lane) {
        require(!f.rmt.channels[lane].pending, "stalled lane abandoned a healthy lane");
        require(std::all_of(f.rmt.channels[lane].frame.begin(), f.rmt.channels[lane].frame.end(), [](auto v) { return v == 0; }), "healthy lane did not black out");
    }
    f.rmt.stall_lane = -1;
    const auto dark = f.controller.blackout();
    require(dark.addressable_confirmed && dark.pwm_confirmed, "delayed completion did not recover to verified zeros");
    require(f.controller.pixels().shadow_pixel(0, 0).r == 0, "recovered zero did not update diagnostic shadow");
    require(std::all_of(f.rmt.channels[0].frame.begin(), f.rmt.channels[0].frame.end(), [](auto v) { return v == 0; }), "recovery exposed stale content");
    f.controller.pixels().fill(0, 51, 63, 75);
    f.controller.pwm().set(0, 600);
    require(f.controller.try_commit() == ESP_OK && f.pca.duties[0] == 600, "fresh output did not resume after recovery");
    require(f.rmt.channels[0].frame[0] == 63 && Pca9685Driver::is_outputs_enabled(), "resumed output did not reach hardware transports");
    f.controller.blackout();
}

void production_transport_nonblocking() {
    host_rmt::active = true;
    host_rmt::stall = true;
    host_rmt::would_block = false;
    auto& transport = production_rmt_transport();
    require(transport.create(0, board::kAledPins[0], false) == ESP_OK, "production transport create failed");
    require(host_rmt::last_channel->depth == 2, "production queue depth changed");
    const uint8_t bytes[] = {17, 31, 47};
    require(transport.submit(0, bytes, sizeof(bytes)) == ESP_OK && transport.submit(0, bytes, sizeof(bytes)) == ESP_OK, "production transport could not queue frames");
    require(transport.submit(0, bytes, sizeof(bytes)) == ESP_ERR_INVALID_STATE, "full production queue did not reject immediately");
    require(host_rmt::last_nonblocking && !host_rmt::would_block, "production transport requested an unbounded queue wait");
    require(transport.wait(0, addressable_timeout_us(1, false)) == ESP_ERR_TIMEOUT, "production wait lost its deadline");
    require(host_rmt::last_wait_ms > 0 && host_rmt::last_wait_ms != -1 && host_rmt::last_channel->pending == 2, "timed-out SDK wait incorrectly discarded transactions");
    host_rmt::stall = false;
    require(transport.wait(0, addressable_timeout_us(1, false)) == ESP_OK && transport.get_completed_count(0) == 2, "production transport completion ledger failed");
    require(transport.destroy(0) == ESP_OK, "production transport cleanup failed");
    host_rmt::active = false;
}

void production_disable_failure_retains_owner() {
    ProductionRmtFixture sdk;
    auto& transport = production_rmt_transport();
    require(transport.create(0, board::kAledPins[0], false) == ESP_OK, "channel create failed");
    const uint8_t bytes[] = {11, 22, 33};
    require(transport.submit(0, bytes, sizeof(bytes)) == ESP_OK && transport.wait(0, 21000) == ESP_OK, "initial frame failed");
    auto* channel = host_rmt::channel_at(board::kAledPins[0]);
    const unsigned encoder_deletes = host_rmt::encoder_deletes;
    host_rmt::disable_fail_gpio = board::kAledPins[0];
    host_rmt::disable_failures = 1;
    require(transport.destroy(0) == ESP_FAIL, "disable failure was hidden");
    require(host_rmt::channel_at(board::kAledPins[0]) == channel && channel->enabled, "disable failure lost live channel ownership");
    require(host_rmt::deletes == 0, "delete was attempted after disable failed");
    require(host_rmt::live_encoders == 2 && host_rmt::encoder_deletes == encoder_deletes, "disable failure freed the owned encoder");
    require(transport.get_completed_count(0) == 1, "disable failure reset the live completion ledger");
    require(transport.submit(0, bytes, sizeof(bytes)) == ESP_ERR_INVALID_STATE, "cleanup-pending channel accepted a new frame");
    require(transport.destroy(0) == ESP_OK, "disable retry could not finish teardown");
    require(host_rmt::channels.empty() && host_rmt::live_encoders == 0 && transport.get_completed_count(0) == 0, "successful teardown left owned resources");
}

void production_delete_failure_retry() {
    ProductionRmtFixture sdk;
    auto& transport = production_rmt_transport();
    require(transport.create(0, board::kAledPins[0], false) == ESP_OK, "channel create failed");
    const uint8_t bytes[] = {4, 5, 6};
    require(transport.submit(0, bytes, sizeof(bytes)) == ESP_OK && transport.wait(0, 21000) == ESP_OK, "initial frame failed");
    auto* channel = host_rmt::channel_at(board::kAledPins[0]);
    const unsigned encoder_deletes = host_rmt::encoder_deletes;
    host_rmt::delete_fail_gpio = board::kAledPins[0];
    host_rmt::delete_failures = 2;
    require(transport.destroy(0) == ESP_FAIL, "channel delete failure was hidden");
    require(!channel->enabled && channel->pin_reset && host_rmt::channels.size() == 1, "failed delete lost disabled channel state");
    require(host_rmt::live_encoders == 2 && host_rmt::encoder_deletes == encoder_deletes, "failed delete freed encoder before channel retirement");
    require(transport.get_completed_count(0) == 1, "failed delete reset completion ledger");
    const unsigned disables = host_rmt::disables;
    require(transport.destroy(0) == ESP_FAIL, "repeated delete failure falsely succeeded");
    require(host_rmt::disables == disables && host_rmt::deletes == 2, "retry disabled an already-disabled SDK channel");
    require(transport.get_completed_count(0) == 1 && host_rmt::live_encoders == 2, "retry corrupted retained ownership");
    require(transport.destroy(0) == ESP_OK, "delete retry could not finish cleanup");
    require(host_rmt::channels.empty() && host_rmt::live_encoders == 0 && transport.get_completed_count(0) == 0, "finished delete retained resources or stale ledger");
}

void production_multilane_retirement_rollback() {
    ProductionRmtFixture sdk;
    ParallelRmtDriver rmt;
    const auto original = config(7);
    require(rmt.reconfigure(original, 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "original topology failed");
    const std::array<const uint8_t*, 3> wires{rmt.wire_data(0), rmt.wire_data(1), rmt.wire_data(2)};
    const auto next = config(9, 0, 13); // retain 0, remove 1 and 2, add 3
    require(rmt.prepare_candidate(next, 0, 0) == ESP_OK, "candidate prepare failed");
    host_rmt::delete_fail_gpio = board::kAledPins[2];
    host_rmt::delete_failures = 1;
    require(rmt.commit_candidate() == ESP_FAIL, "later lane retirement failure falsely committed candidate");
    require(rmt.has_candidate() && !rmt.configured(3), "failed retirement published a new lane");
    for (uint8_t lane = 0; lane < 3; ++lane) {
        require(rmt.configured(lane) && rmt.pixels(lane) == 4 && rmt.wire_data(lane) == wires[lane], "partial retirement swapped or freed old topology buffers");
        require(rmt.wire_data(lane)[0] == 0, "retained old wire is no longer readable");
    }
    require(rmt.lane_state(1) == LaneState::Faulted && rmt.lane_state(2) == LaneState::Faulted, "retired or partially retired lane was left ready");
    require(!host_rmt::channel_at(board::kAledPins[1]) && host_rmt::channel_at(board::kAledPins[2]), "retirement failure occurred at the wrong hardware boundary");
    const unsigned submitted = host_rmt::submissions;
    require(rmt.commit(true).result == ESP_ERR_INVALID_STATE, "unavailable old lane reported a valid blackout");
    require(host_rmt::submissions == submitted + 1, "unavailable lane submitted a frame before recreation");
    rmt.clear_faults();
    require(rmt.lane_state(1) == LaneState::Faulted && rmt.lane_state(2) == LaneState::Faulted, "clear_faults resurrected retired hardware");
    rmt.cancel_candidate();
    require(!rmt.has_candidate() && !host_rmt::channel_at(board::kAledPins[3]), "cancel left the unpublished new lane owned");
    require(rmt.reconfigure(original, 0, 0) == ESP_OK, "old topology could not recreate retired channels");
    require(rmt.commit(true).result == ESP_OK, "rollback channels inherited stale generation or GPIO routing");
    require(production_rmt_transport().get_completed_count(1) == 1 && production_rmt_transport().get_completed_count(2) == 1, "recreated channels did not start fresh completion ledgers");
    require(rmt.diagnostics().lanes[1].completion_mismatches == 0 && rmt.diagnostics().lanes[2].completion_mismatches == 0, "rollback introduced false completion mismatch");
    require(rmt.reconfigure(next, 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "candidate could not be retried after rollback");
    require(rmt.pixels(0) == 13 && rmt.pixels(3) == 13 && !rmt.configured(1) && !rmt.configured(2), "successful retry published the wrong topology");
}

void production_cancel_cleanup_retry() {
    ProductionRmtFixture sdk;
    ParallelRmtDriver rmt;
    require(rmt.reconfigure(config(1), 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "initial topology failed");
    const uint8_t* old_wire = rmt.wire_data(0);
    const auto next = config(3, 0, 9);
    require(rmt.prepare_candidate(next, 0, 0) == ESP_OK, "candidate prepare failed");
    host_rmt::delete_fail_gpio = board::kAledPins[1];
    host_rmt::delete_failures = 2;
    rmt.cancel_candidate();
    require(!rmt.has_candidate() && !rmt.configured(1) && rmt.wire_data(0) == old_wire, "cancel changed published topology");
    require(host_rmt::channel_at(board::kAledPins[1]) && host_rmt::live_encoders == 4, "failed cancellation forgot channel or encoder ownership");
    const unsigned creates = host_rmt::creates;
    require(rmt.reconfigure(next, 0, 0) == ESP_FAIL, "retained cleanup failure did not propagate through configure");
    require(host_rmt::creates == creates && rmt.wire_data(0) == old_wire && rmt.pixels(0) == 4, "failed cleanup allocated a replacement or published candidate");
    require(rmt.commit().result == ESP_OK, "cancelled candidate broke the retained old channel");
    host_rmt::clear_faults();
    require(rmt.reconfigure(next, 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "cleanup retry could not activate the new lane");
    require(host_rmt::channels.size() == 2 && host_rmt::live_encoders == 4, "cleanup retry leaked or duplicated SDK resources");
}

void production_candidate_direct_retry() {
    ProductionRmtFixture sdk;
    ParallelRmtDriver rmt;
    require(rmt.reconfigure(config(7), 0, 0) == ESP_OK && rmt.commit().result == ESP_OK, "initial topology failed");
    const auto next = config(1, 0, 11);
    require(rmt.prepare_candidate(next, 0, 0) == ESP_OK, "candidate prepare failed");
    const uint8_t* old_wire = rmt.wire_data(0);
    host_rmt::disable_fail_gpio = board::kAledPins[2];
    host_rmt::disable_failures = 1;
    require(rmt.commit_candidate() == ESP_FAIL, "disable failure falsely committed candidate");
    require(rmt.has_candidate() && rmt.wire_data(0) == old_wire && rmt.pixels(0) == 4, "failure published the retained lane's candidate");
    require(rmt.commit_candidate() == ESP_OK, "direct candidate retry could not finish retirement");
    require(!rmt.has_candidate() && rmt.pixels(0) == 11 && !rmt.configured(1) && !rmt.configured(2), "direct retry published a partial topology");
    require(rmt.commit().result == ESP_OK && production_rmt_transport().get_completed_count(0) == 2, "direct retry lost the retained channel generation");
    require(host_rmt::channels.size() == 1 && host_rmt::live_encoders == 2, "direct retry leaked pending or retired resources");
}

void production_create_cleanup_retry() {
    ProductionRmtFixture sdk;
    auto& transport = production_rmt_transport();
    host_rmt::callbacks_fail_gpio = board::kAledPins[0];
    host_rmt::callbacks_failures = 1;
    host_rmt::delete_fail_gpio = board::kAledPins[0];
    host_rmt::delete_failures = 2;
    require(transport.create(0, board::kAledPins[0], false) == ESP_FAIL, "callback registration failure hidden");
    require(host_rmt::channels.size() == 1 && host_rmt::live_encoders == 0, "failed creation did not retain cleanup ownership");
    require(transport.create(0, board::kAledPins[0], false) == ESP_FAIL, "creation retried over unresolved cleanup");
    require(host_rmt::creates == 1 && host_rmt::channels.size() == 1, "failed cleanup leaked a channel slot");
    require(transport.create(0, board::kAledPins[0], false) == ESP_OK, "completed cleanup did not permit creation retry");
    require(host_rmt::creates == 2 && host_rmt::channels.size() == 1 && host_rmt::live_encoders == 2, "creation retry retained stale resources");
}

void production_destructor_retains_failed_teardown() {
    ProductionRmtFixture sdk;
    auto rmt = std::make_unique<ParallelRmtDriver>();
    require(rmt->reconfigure(config(1), 0, 0) == ESP_OK && rmt->commit().result == ESP_OK, "initial topology failed");
    const uint8_t* wire = rmt->wire_data(0);
    host_rmt::delete_fail_gpio = board::kAledPins[0];
    host_rmt::delete_failures = 100;
    rmt.reset();
    require(host_rmt::channel_at(board::kAledPins[0]) && host_rmt::live_encoders == 2, "destructor forgot a failed channel teardown");
    require(production_rmt_transport().get_completed_count(0) == 1 && wire[0] == 0, "destructor freed retained wire or reset the owned ledger");
    require(host_rmt::disables == 1 && host_rmt::deletes == 1, "destructor looped on persistent teardown failure");
    host_rmt::clear_faults();
    require(production_rmt_transport().destroy(0) == ESP_OK, "retained transport resource could not be released after failure cleared");
    require(host_rmt::channels.empty() && host_rmt::live_encoders == 0, "persistent-failure cleanup left SDK resources");
    // The destroyed driver intentionally retained this quarantined allocation.
    // After the fake SDK confirms teardown, release it explicitly for the test.
    heap_caps_free(const_cast<uint8_t*>(wire));
}

void production_controller_rollback_fail_closed() {
    ProductionRmtFixture sdk;
    PcaTransport pca;
    OutputController controller;
    controller.pwm().set_transport(&pca);
    require(controller.init() == ESP_OK && controller.reconfigure(config(7, 1)) == ESP_OK, "controller initial topology failed");
    const uint32_t generation = controller.topology_generation();
    host_rmt::delete_fail_gpio = board::kAledPins[2];
    host_rmt::delete_failures = 100;
    require(controller.reconfigure(config(9, 1, 13)) == ESP_FAIL, "persistent retirement failure was hidden by controller");
    require(controller.topology_generation() == generation && !controller.is_topology_valid() && !controller.output_enabled(), "failed rollback published a candidate or kept outputs enabled");
    require(controller.topology_rollback_failures() == 1, "failed rollback diagnostics missing");
    require_disabled("failed rollback exposed PWM");
    host_rmt::clear_faults();
    require(controller.rollback_to_last_good_topology() == ESP_OK && controller.is_topology_valid(), "explicit retry could not restore old topology");
    require(controller.topology_generation() == generation && controller.pixels().configured_pixels(1) == 4 && controller.pixels().configured_pixels(2) == 4 && !controller.pixels().configured(3), "rollback restored a mixed topology");
}

void production_pm_delete_quarantine() {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
    // Run in a separate PM-enabled process. The partially destroyed SDK object
    // must stay quarantined until process/device reset; no test cleanup retries
    // its destructor or frees the encoder/wire behind the retained ownership.
    host_rmt::active = true;
    host_rmt::clear_faults();
    auto rmt = std::make_unique<ParallelRmtDriver>();
    require(rmt->reconfigure(config(1), 0, 0) == ESP_OK && rmt->commit().result == ESP_OK, "initial topology failed");
    const uint8_t* wire = rmt->wire_data(0);
    host_rmt::delete_fail_gpio = board::kAledPins[0];
    host_rmt::delete_failures = 1;
    host_rmt::partial_delete_failure = true;
    require(rmt->release_lane(0) == ESP_FAIL, "partial SDK deletion failure hidden");
    require(host_rmt::channel_at(board::kAledPins[0])->interrupt_freed, "test did not reach partial SDK teardown");
    const unsigned deletes = host_rmt::deletes;
    for (unsigned retry = 0; retry < 3; ++retry) {
        require(rmt->release_lane(0) == ESP_FAIL, "unsafe partial teardown retry reported success");
        require(rmt->reconfigure(config(1), 0, 0) == ESP_FAIL, "reconfigure escaped the partial-delete quarantine");
    }
    require(host_rmt::deletes == deletes && host_rmt::unsafe_delete_retries == 0, "retry reused an SDK object with freed interrupt state");
    require(host_rmt::live_encoders == 2 && wire[0] == 0 && production_rmt_transport().get_completed_count(0) == 1, "quarantine lost encoder/wire/ledger ownership");
    rmt.reset();
    require(host_rmt::deletes == deletes && host_rmt::live_encoders == 2 && wire[0] == 0, "destructor bypassed partial-delete quarantine");
#else
    throw std::runtime_error("PM quarantine case requires CONFIG_PM_ENABLE=1");
#endif
}

void controller_canonical_power_zero() {
    ControllerFixture f;
    auto cfg = config(0, 1);
    auto& ch = cfg.channels[8];
    ch.max_milliwatts = 6000;
    ch.rated_full_power_mw = 54321;
    ch.channel_limit_mw = 0;
    f.start(cfg);
    f.controller.pwm().set(0, 4095);
    require(f.controller.try_commit() == ESP_OK && f.pca.duties[0] == 4095, "v4 zero limit revived a stale 6000mW legacy cap");
    ch.rated_full_power_mw = 0;
    ch.channel_limit_mw = 1000;
    cfg.max_power_mw = 1000;
    f.controller.set_power_config(cfg);
    f.controller.pwm().set(0, 4095);
    require(f.controller.try_commit() == ESP_OK && f.pca.duties[0] == 4095, "v4 unknown rating fell back to stale legacy power");
    ch.rated_full_power_mw = 54321;
    cfg.max_power_mw = 0;
    f.controller.set_power_config(cfg);
    f.controller.pwm().set(0, 4095);
    require(f.controller.try_commit() == ESP_OK && f.pca.duties[0] > 0 && f.pca.duties[0] < 4095, "positive canonical limit stopped working");
    cfg.schema_version = 3;
    cfg.max_power_mw = 1000;
    ch.rated_full_power_mw = ch.channel_limit_mw = 0;
    f.controller.set_power_config(cfg);
    f.controller.pwm().set(0, 4095);
    require(f.controller.try_commit() == ESP_OK && f.pca.duties[0] < 4095, "legacy schema power fallback stopped working");
    f.controller.blackout();
}

// Inject a stop after the commit's initial gate while a hardware write is in
// progress. A recursive callback models the interleaving deterministically;
// production stop uses the other core and must not be undone by the final gate.
void controller_estop_during_commit() {
    ControllerFixture f;
    f.start(config(0, 1));
    f.controller.pwm().set(0, 700);
    f.pca.frame_hook = [&] { f.controller.latch_estop(); };
    require(f.controller.try_commit() == ESP_ERR_INVALID_STATE, "commit acknowledged output after an intervening emergency stop");
    require(f.controller.is_estop_latched() && !f.controller.output_enabled(), "commit cleared the emergency stop gate");
    require_disabled("commit re-enabled PWM after emergency stop");
    require(f.controller.last_fault_domain() == OutputFaultDomain::Safety, "commit hid the emergency stop fault domain");
    require(f.controller.clear_estop() == ESP_OK, "stop could not be explicitly cleared");
    f.controller.pwm().set(0, 200);
    require(f.controller.try_commit() == ESP_OK, "fresh output failed after stop clearance");
    f.controller.blackout();
}

void controller_new_estop_during_clear() {
    ControllerFixture f;
    f.start(config(0, 1));
    f.controller.latch_estop();
    f.pca.frame_hook = [&] { f.controller.latch_estop(); };
    require(f.controller.clear_estop() == ESP_ERR_INVALID_STATE, "clear erased a newer emergency stop while zeroing hardware");
    require(f.controller.is_estop_latched() && !f.controller.output_enabled(), "new emergency stop was not retained");
    require_disabled("clear exposed PWM after a newer stop");
    require(f.controller.clear_estop() == ESP_OK, "later explicit stop clearance failed");
    require_disabled("clear restored previous PWM content");
}

using Test = std::pair<const char*, void (*)()>;
const Test tests[] = {
    {"controller_estop_during_commit", controller_estop_during_commit},
    {"controller_new_estop_during_clear", controller_new_estop_during_clear},
    {"pca_nonzero_enable", pca_nonzero_enable},
    {"pca_fault_latch", pca_fault_latch},
    {"pca_failed_recovery", pca_failed_recovery},
    {"rmt_recreated_lane", rmt_recreated_lane},
    {"rmt_retained_lane", rmt_retained_lane},
    {"rmt_cancel_candidate", rmt_cancel_candidate},
    {"rmt_completion_guards", rmt_completion_guards},
    {"controller_mixed_output", controller_mixed_output},
    {"controller_disabled_pwm", controller_disabled_pwm},
    {"controller_enable_failure", controller_enable_failure},
    {"controller_initial_recovery", controller_initial_recovery},
    {"controller_recovery_preserves_estop", controller_recovery_preserves_estop},
    {"controller_lane_toggle", controller_lane_toggle},
    {"rmt_lease_blocks_reconfigure", rmt_lease_blocks_reconfigure},
    {"rmt_destructor_retains_lease", rmt_destructor_retains_lease},
    {"controller_timeout_quarantine", controller_timeout_quarantine},
    {"production_transport_nonblocking", production_transport_nonblocking},
    {"production_disable_failure_retains_owner", production_disable_failure_retains_owner},
    {"production_delete_failure_retry", production_delete_failure_retry},
    {"production_multilane_retirement_rollback", production_multilane_retirement_rollback},
    {"production_cancel_cleanup_retry", production_cancel_cleanup_retry},
    {"production_candidate_direct_retry", production_candidate_direct_retry},
    {"production_create_cleanup_retry", production_create_cleanup_retry},
    {"production_destructor_retains_failed_teardown", production_destructor_retains_failed_teardown},
    {"production_controller_rollback_fail_closed", production_controller_rollback_fail_closed},
    {"production_pm_delete_quarantine", production_pm_delete_quarantine},
    {"controller_canonical_power_zero", controller_canonical_power_zero},
};
}

int main(int argc, char** argv) {
    unsigned passed = 0, failed = 0;
    for (int arg = 1; arg < argc; ++arg) {
        require(std::any_of(std::begin(tests), std::end(tests), [&](const auto& t) { return t.first == std::string(argv[arg]); }), "unknown test group");
    }
    for (const auto& [name, test] : tests) {
        bool selected = argc == 1;
#if !defined(CONFIG_PM_ENABLE) || !CONFIG_PM_ENABLE
        if (std::string(name) == "production_pm_delete_quarantine") selected = false;
#endif
        for (int arg = 1; arg < argc; ++arg) selected |= name == std::string(argv[arg]);
        if (!selected) continue;
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        Pca9685Driver::force_disable_outputs();
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
