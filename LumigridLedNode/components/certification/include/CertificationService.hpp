// components/certification/include/CertificationService.hpp
#pragma once

#include "CertificationTypes.hpp"
#include "CertificationPlan.hpp"
#include "CertificationStore.hpp"
#include "NodeIdentity.hpp"
#include "SignageTypes.hpp"
#include "esp_err.h"
#include <cstdint>
#include <cstddef>
struct cJSON;

namespace lg {

class FactoryRunner;
class ConfigStore;

struct CertificationStatusSummary {
    CertificationState state = CertificationState::NotStarted;
    char current_test[16] = {0};
    uint16_t passed = 0;
    uint16_t failed = 0;
    uint16_t total = 0;
    bool is_stale = false;
    char stale_reason[64] = {0};
};

class CertificationService {
public:
    CertificationService();
    ~CertificationService() = default;

    esp_err_t init(ConfigStore* config, FactoryRunner* runner, const NodeIdentity& identity);

    // Session lifecycle
    esp_err_t start_session(CertificationProfile profile, const char* serial_number = nullptr);
    esp_err_t abort();

    // Test execution
    esp_err_t start_test(const char* test_id);
    esp_err_t record_result(const char* test_id,
                            CertificationTestState state,
                            EvidenceSource source,
                            float measured_value = 0.0f,
                            const char* unit = "",
                            const char* note = "");

    // Status & queries
    const CertificationSession& session() const { return store_.current_record().session; }
    const CertificationRecord& record() const { return store_.current_record(); }
    const CertificationTestResult* get_test_result(const char* test_id) const;

    bool is_in_progress() const { return store_.current_record().session.state == CertificationState::InProgress; }
    const char* current_running_test() const { return current_test_; }

    // Stale & Invalidation detection
    void check_invalidation(const DeviceConfig& current_cfg, const char* current_hw_rev, const char* current_fw_ver);
    bool is_stale() const { return is_stale_; }
    const char* stale_reason() const { return stale_reason_; }

    // Burn-in tracking
    void update_burn_in_tick(uint32_t elapsed_s, uint32_t commit_fails, uint32_t output_faults);
    void finish_burn_in(bool passed, const char* reason = nullptr);

    // Compact summary for RuntimeSnapshot (Spec §127)
    CertificationStatusSummary get_status_summary() const;

    // Report generation (cJSON root object, caller frees with cJSON_Delete)
    // NEVER contains secrets/tokens (Spec §124)
    cJSON* generate_report_json() const;

    // Helper: compute hardware-relevant configuration fingerprint
    static uint32_t compute_hardware_fingerprint(const DeviceConfig& cfg, const char* hw_rev);

private:
    void evaluate_overall_state();
    int find_result_index(const char* test_id) const;

    ConfigStore* config_ = nullptr;
    FactoryRunner* runner_ = nullptr;
    NodeIdentity identity_{};
    CertificationStore store_{};

    char current_test_[16] = {0};
    uint64_t current_test_started_ms_ = 0;
    bool is_stale_ = false;
    char stale_reason_[64] = {0};

    // Burn-in state
    bool burn_in_active_ = false;
    uint32_t burn_in_target_s_ = 0;
    uint64_t burn_in_start_ms_ = 0;
};

} // namespace lg
