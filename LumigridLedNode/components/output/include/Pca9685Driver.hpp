#pragma once
#include <array>
#include <cstdint>
#include "esp_err.h"
#include "SignageTypes.hpp"

namespace lg {

constexpr uint32_t kPcaTransactionTimeoutMs = 10;

enum class PcaFaultKind : uint8_t {
    None = 0,
    TransportError,
    Timeout,
    DeviceNotResponding,
    SdaStuckLow,
    SclStuckLow,
    RegisterVerificationFailed,
    RecoveryFailed,
    OutputGateFailed,
};

inline const char* pca_fault_kind_name(PcaFaultKind k) {
    switch (k) {
        case PcaFaultKind::None: return "none";
        case PcaFaultKind::TransportError: return "transport_error";
        case PcaFaultKind::Timeout: return "timeout";
        case PcaFaultKind::DeviceNotResponding: return "device_not_responding";
        case PcaFaultKind::SdaStuckLow: return "sda_stuck_low";
        case PcaFaultKind::SclStuckLow: return "scl_stuck_low";
        case PcaFaultKind::RegisterVerificationFailed: return "register_verification_failed";
        case PcaFaultKind::RecoveryFailed: return "recovery_failed";
        case PcaFaultKind::OutputGateFailed: return "output_gate_failed";
        default: return "unknown";
    }
}

struct PcaBusState {
    bool sda_high = true;
    bool scl_high = true;
};

struct PcaDiagnostics {
    bool initialized = false;
    bool outputs_enabled = false;
    bool healthy = true;
    bool recovery_required = false;

    uint8_t address = 0;
    uint32_t i2c_hz = 100000;
    uint32_t pwm_hz = 1017;

    uint8_t mode1 = 0;
    uint8_t mode2 = 0;
    uint8_t prescale = 0;

    uint32_t commit_failures = 0;

    uint32_t transactions = 0;
    uint32_t transaction_failures = 0;
    uint32_t consecutive_failures = 0;
    esp_err_t last_error = ESP_OK;
    int init_core = -1;

    PcaFaultKind last_fault_kind = PcaFaultKind::None;
    bool last_sda_high = true;
    bool last_scl_high = true;

    uint32_t last_transaction_us = 0;
    uint32_t max_transaction_us = 0;
    uint64_t total_transaction_us = 0;

    uint32_t recovery_attempts = 0;
    uint32_t recovery_successes = 0;
    uint32_t recovery_failures = 0;

    uint32_t bus_clear_attempts = 0;
    uint32_t bus_clear_successes = 0;
    uint32_t bus_clear_failures = 0;

    uint32_t driver_reinstalls = 0;
};

class IPcaTransport {
public:
    virtual ~IPcaTransport() = default;
    virtual esp_err_t init_bus() = 0;
    virtual esp_err_t deinit_bus() = 0;
    virtual bool is_bus_initialized() const = 0;
    virtual esp_err_t write(uint8_t addr, const uint8_t* data, size_t len, uint32_t timeout_ms) = 0;
    virtual esp_err_t write_read(uint8_t addr, const uint8_t* write_data, size_t write_len,
                                 uint8_t* read_data, size_t read_len, uint32_t timeout_ms) = 0;
    virtual bool sda_high() const = 0;
    virtual bool scl_high() const = 0;
    virtual esp_err_t recover_bus(uint32_t& pulses_sent, bool& sda_released) = 0;
};

class Pca9685Driver {
public:
    static constexpr size_t kChannels = MAX_PWM_CHANNELS;

    explicit Pca9685Driver(IPcaTransport* transport = nullptr);
    ~Pca9685Driver() = default;

    void set_transport(IPcaTransport* transport) {
        transport_ = transport;
    }

    esp_err_t init();
    esp_err_t recover();

    esp_err_t initialise_bus();
    esp_err_t initialise_chip();
    esp_err_t verify_chip();
    esp_err_t write_zero_frame();

    esp_err_t set(uint8_t channel, uint16_t duty);
    esp_err_t set_all(uint16_t duty);
    esp_err_t clear();
    esp_err_t commit();

    // Immediately request /OE HIGH without I2C. Return rejected GPIO writes;
    // the enabled flag changes only after a successful GPIO operation.
    static esp_err_t force_disable_outputs();

    // Output enable control: owned by OutputController safety state
    esp_err_t enable_outputs();
    static esp_err_t disable_outputs();
    static bool is_outputs_enabled();
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    static void enable_test_output_mock();
#endif

    bool zero_confirmed() const { return zero_confirmed_; }
    bool recovery_required() const;
    PcaFaultKind last_fault_kind() const;

    PcaDiagnostics diagnostics() const;

    const std::array<uint16_t, kChannels>& values() const {
        return values_;
    }

private:
    IPcaTransport& transport();
    void classify_and_record_failure(esp_err_t err);

    esp_err_t read_register(uint8_t reg, uint8_t& value);
    esp_err_t write_register(uint8_t reg, uint8_t value);
    esp_err_t write_bytes(uint8_t start_reg, const uint8_t* data, size_t len);

    IPcaTransport* transport_ = nullptr;
    std::array<uint16_t, kChannels> values_{};

    bool initialized_ = false;
    bool zero_confirmed_ = false;
    bool valid_frame_confirmed_ = false;
    bool recovery_required_ = false;
    bool register_mismatch_ = false;

    uint8_t mode1_ = 0;
    uint8_t mode2_ = 0;
    uint8_t prescale_ = 0;
    uint32_t commit_failures_ = 0;

    uint32_t transactions_ = 0;
    uint32_t transaction_failures_ = 0;
    uint32_t consecutive_failures_ = 0;
    esp_err_t last_error_ = ESP_OK;
    int init_core_ = -1;

    PcaFaultKind last_fault_kind_ = PcaFaultKind::None;
    bool last_sda_high_ = true;
    bool last_scl_high_ = true;

    uint32_t last_transaction_us_ = 0;
    uint32_t max_transaction_us_ = 0;
    uint64_t total_transaction_us_ = 0;

    uint32_t recovery_attempts_ = 0;
    uint32_t recovery_successes_ = 0;
    uint32_t recovery_failures_ = 0;

    uint32_t bus_clear_attempts_ = 0;
    uint32_t bus_clear_successes_ = 0;
    uint32_t bus_clear_failures_ = 0;

    uint32_t driver_reinstalls_ = 0;
};

} // namespace lg
