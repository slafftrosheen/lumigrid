// components/certification/CertificationService.cpp
#include "CertificationService.hpp"
#include "FactoryRunner.hpp"
#include "ConfigStore.hpp"
#include "JobManager.hpp"
#include "RuntimeMemory.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "cJSON.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace lg {

namespace {

static const char* TAG = "cert_service";

static uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

} // namespace

CertificationService::CertificationService() = default;

esp_err_t CertificationService::init(ConfigStore* config,
                                     FactoryRunner* runner,
                                     JobManager* job_mgr,
                                     const NodeIdentity& identity,
                                     CertificationProbeProvider probe_provider) {
    config_ = config;
    runner_ = runner;
    job_mgr_ = job_mgr;
    identity_ = identity;
    probe_provider_ = std::move(probe_provider);

    esp_err_t err = store_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CertificationStore: %s", esp_err_to_name(err));
        return err;
    }

    if (config_) {
        std::unique_ptr<DeviceConfig> cfg(new (std::nothrow) DeviceConfig{});
        if (!cfg) return ESP_ERR_NO_MEM;
        err = config_->load_config(*cfg);
        if (err != ESP_OK) return err;
        check_invalidation(*cfg, identity_.hw_revision, identity_.fw_version);
    }

    return ESP_OK;
}

uint32_t CertificationService::compute_hardware_fingerprint(const DeviceConfig& cfg, const char* hw_rev) {
    uint32_t crc = 0;
    if (hw_rev) {
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(hw_rev), strlen(hw_rev));
    }

    for (size_t i = 0; i < 16; ++i) {
        const auto& ch = cfg.channels[i];
        const uint8_t en = ch.enabled ? 1 : 0;
        crc = esp_rom_crc32_le(crc, &en, sizeof(en));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(ch.name), strlen(ch.name));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&ch.addressable_type), sizeof(ch.addressable_type));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&ch.pixel_order), sizeof(ch.pixel_order));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&ch.pixel_count), sizeof(ch.pixel_count));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&ch.max_milliwatts), sizeof(ch.max_milliwatts));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&ch.rated_full_power_mw), sizeof(ch.rated_full_power_mw));
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&ch.channel_limit_mw), sizeof(ch.channel_limit_mw));
    }

    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&cfg.max_power_mw), sizeof(cfg.max_power_mw));
    for (size_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& psu = cfg.power_supplies[s];
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&psu), sizeof(psu));
    }
    return crc;
}

esp_err_t CertificationService::start_session(CertificationProfile profile, const char* serial_number) {
    if (!store_.is_initialized() || is_in_progress() || current_test_[0] != '\0' || burn_in_active_ || cancellation_pending_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (profile > CertificationProfile::ReleaseCandidate ||
        CertificationPlan::test_count() > kMaxCertificationTests) {
        return ESP_ERR_INVALID_ARG;
    }
    const char* sn = serial_number;
    std::unique_ptr<DeviceConfig> cfg;
    if (config_) {
        cfg.reset(new (std::nothrow) DeviceConfig{});
        if (!cfg) return ESP_ERR_NO_MEM;
        const esp_err_t config_err = config_->load_config(*cfg);
        if (config_err != ESP_OK) return config_err;
        if (!sn || sn[0] == '\0') {
            sn = cfg->serial_number;
        }
    }

    // Reject evidence that would be silently truncated in the durable record.
    if (sn && strlen(sn) >= sizeof(CertificationIdentity{}.serial_number)) {
        return ESP_ERR_INVALID_ARG;
    }
    bool has_serial = false;
    for (const char* p = sn; p && *p; ++p) {
        if (!std::isspace(static_cast<unsigned char>(*p))) has_serial = true;
    }
    // Spec §102: ProductionUnit requires non-empty serial number
    if (profile == CertificationProfile::ProductionUnit) {
        if (!has_serial) {
            ESP_LOGE(TAG, "BLOCKED: serial number required for ProductionUnit certification");
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Spec §103: ProductionUnit and ReleaseCandidate require valid hardware revision
    if (profile == CertificationProfile::ProductionUnit || profile == CertificationProfile::ReleaseCandidate) {
        if (identity_.hw_revision[0] == '\0' || strcmp(identity_.hw_revision, "unknown") == 0) {
            ESP_LOGE(TAG, "BLOCKED: valid hardware revision required");
            return ESP_ERR_INVALID_ARG;
        }
    }

    CertificationRecord& rec = store_.current_record();
    reset_certification_record(rec);
    rec.schema_version = kCertificationRecordSchemaVersion;

    // Uptime alone repeats after reboot. Keep each immutable evidence session
    // distinct, including sessions started at the same uptime on one node.
    const uint32_t random_high = esp_random();
    const uint32_t random_low = esp_random();
    snprintf(rec.session.identity.certification_id, sizeof(rec.session.identity.certification_id),
             "cert-%.8s-%08lx%08lx", identity_.id,
             static_cast<unsigned long>(random_high), static_cast<unsigned long>(random_low));
    strncpy(rec.session.identity.node_id, identity_.id, sizeof(rec.session.identity.node_id) - 1);
    strncpy(rec.session.identity.firmware_version, identity_.fw_version, sizeof(rec.session.identity.firmware_version) - 1);
    strncpy(rec.session.identity.project_name, "LumiGrid", sizeof(rec.session.identity.project_name) - 1);
    strncpy(rec.session.identity.hardware_revision, identity_.hw_revision, sizeof(rec.session.identity.hardware_revision) - 1);
    if (sn) {
        strncpy(rec.session.identity.serial_number, sn, sizeof(rec.session.identity.serial_number) - 1);
    }
    rec.session.identity.plan_version = kCertificationPlanVersion;

    rec.session.profile = profile;
    rec.session.state = CertificationState::InProgress;
    rec.session.started_ms = esp_timer_get_time() / 1000ULL;
    rec.session.completed_ms = 0;
    rec.session.test_count = static_cast<uint16_t>(CertificationPlan::test_count());
    rec.session.passed_count = 0;
    rec.session.failed_count = 0;
    rec.session.skipped_count = 0;

    if (config_) {
        rec.session.config_fingerprint = compute_hardware_fingerprint(*cfg, identity_.hw_revision);
    }

    // Initialize all test results
    rec.result_count = static_cast<uint16_t>(CertificationPlan::test_count());
    const auto* defs = CertificationPlan::test_definitions();
    for (size_t i = 0; i < rec.result_count; ++i) {
        strncpy(rec.results[i].test_id, defs[i].id, sizeof(rec.results[i].test_id) - 1);
        rec.results[i].state = CertificationTestState::Pending;
        rec.results[i].evidence_source = defs[i].required_evidence;
        rec.results[i].started_ms = 0;
        rec.results[i].completed_ms = 0;
        rec.results[i].error_code = 0;
        rec.results[i].measured_value = 0.0f;
        strncpy(rec.results[i].unit, defs[i].unit, sizeof(rec.results[i].unit) - 1);
        rec.results[i].note[0] = '\0';
        rec.results[i].attempts = 0;
    }

    current_test_[0] = '\0';
    current_test_started_ms_ = 0;
    confirmation_pending_ = false;
    is_stale_ = false;
    stale_reason_[0] = '\0';

    ESP_LOGI(TAG, "Started new certification session %s (profile=%s, tests=%u)",
             rec.session.identity.certification_id,
             certification_profile_to_string(profile),
             rec.result_count);

    return persist_record();
}

int CertificationService::find_result_index(const char* test_id) const {
    if (!test_id) return -1;
    const auto& rec = store_.current_record();
    for (size_t i = 0; i < rec.result_count && i < kMaxCertificationTests; ++i) {
        if (strcmp(rec.results[i].test_id, test_id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const CertificationTestResult* CertificationService::get_test_result(const char* test_id) const {
    int idx = find_result_index(test_id);
    if (idx < 0) return nullptr;
    return &store_.current_record().results[idx];
}

esp_err_t CertificationService::start_test(const char* test_id) {
    if (!is_in_progress() || current_test_[0] != '\0' || burn_in_active_ || cancellation_pending_) {
        ESP_LOGE(TAG, "Cannot start test %s: no certification session in progress", test_id ? test_id : "");
        return ESP_ERR_INVALID_STATE;
    }

    const auto* def = CertificationPlan::find_test(test_id);
    if (!def) {
        ESP_LOGE(TAG, "Unknown certification test: %s", test_id ? test_id : "");
        return ESP_ERR_NOT_FOUND;
    }

    int idx = find_result_index(test_id);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    CertificationRecord& rec = store_.current_record();
    CertificationTestResult& res = rec.results[idx];
    // Attempts identify an execution, not the number of result submissions.
    // Never wrap a binding and let an old client target a later attempt.
    if (res.attempts == UINT8_MAX) return ESP_ERR_INVALID_STATE;
    ++res.attempts;

    current_factory_session_id_ = 0;
    strncpy(current_test_, test_id, sizeof(current_test_) - 1);
    current_test_[sizeof(current_test_) - 1] = '\0';
    current_test_started_ms_ = now_ms();

    res.state = CertificationTestState::Running;
    res.started_ms = esp_timer_get_time() / 1000ULL;
    res.completed_ms = 0;
    res.error_code = 0;
    res.evidence_source = def->required_evidence;
    res.measured_value = 0.0f;
    snprintf(res.unit, sizeof(res.unit), "%s", def->unit);
    res.note[0] = '\0';

    // Route physical tests to FactoryRunner.
    // LG-038.3 §20: holding a FactoryRunner pointer is fine, but automatic
    // probes below never touch it — the worker only starts when a physical
    // test actually begins (via start_test_session's lazy start). Probe
    // collection allocates no Factory stack.
    const auto start_physical_test = [this, &res, test_id](FactoryTestKind kind, uint8_t channel) {
        const bool accepted = runner_ && runner_->start_test_session(
            kind, channel, 10000, 32, &current_factory_session_id_,
            FactorySessionOwner::Certification, test_id);
        if (!accepted) {
            // The runner assigns its proposed ID before queue admission.
            // A rejected command never owns that session or its outputs.
            current_factory_session_id_ = 0;
            res.state = CertificationTestState::Blocked;
            res.error_code = -1;
            snprintf(res.note, sizeof(res.note), "FactoryRunner unavailable");
        }
    };
    if (strncmp(test_id, "ADDR-A", 6) == 0 && strlen(test_id) == 7) {
        uint8_t ch = static_cast<uint8_t>(test_id[6] - '1');
        if (ch < 8) start_physical_test(FactoryTestKind::AddressableChannel, ch);
    } else if (strcmp(test_id, "ADDR-XTALK") == 0) {
        start_physical_test(FactoryTestKind::AddressableChannel, 0);
    } else if (strncmp(test_id, "PWM-P", 5) == 0 && strlen(test_id) == 6) {
        uint8_t p = static_cast<uint8_t>(test_id[5] - '1');
        if (p < 8) start_physical_test(FactoryTestKind::PwmChannel, p + 8);
    } else if (strcmp(test_id, "PWM-XTALK") == 0) {
        start_physical_test(FactoryTestKind::PwmChannel, 8);
    } else if (strcmp(test_id, "SAFE-ESTOP-ADDRESSABLE") == 0 ||
               strcmp(test_id, "SAFE-ESTOP-PWM") == 0 ||
               strcmp(test_id, "SAFE-DISABLE-001") == 0) {
        start_physical_test(FactoryTestKind::SafetyGate, 0xFF);
    } else if (strcmp(test_id, "BOOT-OUTPUT-SAFE") == 0) {
        // Operator observes boot safe state
        res.state = CertificationTestState::WaitingForOperator;
    } else if (strcmp(test_id, "BURN-001") == 0) {
        uint32_t duration_minutes = 5;
        if (rec.session.profile == CertificationProfile::ProductionUnit) {
            duration_minutes = 30;
        } else if (rec.session.profile == CertificationProfile::ReleaseCandidate) {
            duration_minutes = 60;
        }
        if (runner_ && job_mgr_) {
            uint32_t jid = 0;
            bool ok = runner_->start_burn_in_job(job_mgr_, duration_minutes, FactoryRunner::kBurnInDefaultBrightness, &jid);
            if (ok) {
                burn_in_active_ = true;
                burn_in_job_id_ = jid;
                burn_in_target_s_ = duration_minutes * 60;
                burn_in_start_ms_ = esp_timer_get_time() / 1000ULL;
                rec.burn_in.start_free_heap = static_cast<uint32_t>(esp_get_free_heap_size());
                rec.burn_in.min_free_heap = rec.burn_in.start_free_heap;
                // LG-038.3 §74: burn-in memory evidence uses the canonical
                // 8-bit snapshot (start/min/end recorded across the run).
                const RuntimeMemorySnapshot burn_in_mem = capture_runtime_memory();
                burn_in_start_free8_ = burn_in_mem.free_8bit;
                burn_in_min_free8_ = burn_in_mem.free_8bit;
                burn_in_start_largest8_ = burn_in_mem.largest_8bit;
                burn_in_min_largest8_ = burn_in_mem.largest_8bit;
                burn_in_end_free8_ = 0;
                burn_in_end_largest8_ = 0;
                rec.burn_in.duration_s = 0;
                res.state = CertificationTestState::Running;
            } else {
                res.state = CertificationTestState::Blocked;
                res.error_code = -1;
                snprintf(res.note, sizeof(res.note), "Failed to start burn-in job");
            }
        } else {
            res.state = CertificationTestState::Blocked;
            res.error_code = -1;
            snprintf(res.note, sizeof(res.note), "JobManager or FactoryRunner unavailable");
        }
    } else if (def->required_evidence == EvidenceSource::Automatic) {
        CertificationProbeSnapshot probe{};
        if (probe_provider_) {
            probe = probe_provider_();
        }

        bool auto_pass = false;
        bool auto_fail = false;
        bool auto_blocked = false;
        const char* note_str = nullptr;

        if (strcmp(test_id, "BOOT-001") == 0) {
            auto_pass = (identity_.id[0] != '\0' && identity_.fw_version[0] != '\0');
            if (!auto_pass) note_str = "Identity missing";
        } else if (strcmp(test_id, "BOOT-REBOOT-001") == 0) {
            auto_blocked = true;
            note_str = "Reboot persistence requires before/after fixture evidence";
        } else if (strcmp(test_id, "SAFE-ESTOP-LOGIC") == 0) {
            auto_blocked = true;
            note_str = "E-stop latch/suppression requires an executed fixture test";
        } else if (strcmp(test_id, "NET-AP-001") == 0) {
            if (probe.ap_active) {
                auto_pass = true;
            } else {
                auto_fail = true;
                note_str = "AP inactive";
            }
        } else if (strcmp(test_id, "NET-STA-001") == 0) {
            if (probe.sta_connected) {
                auto_pass = true;
            } else if (def->requirement_for_profile(rec.session.profile) == RequirementLevel::Required) {
                auto_fail = true;
                note_str = "STA not connected";
            } else {
                res.state = CertificationTestState::Skipped;
                note_str = "STA not connected (optional)";
            }
        } else if (strcmp(test_id, "API-001") == 0) {
            if (probe.api_running) {
                auto_pass = true;
            } else {
                auto_blocked = true;
                note_str = "API server not running";
            }
        } else if (strcmp(test_id, "WS-001") == 0) {
            if (probe.authenticated_ws_clients > 0) {
                auto_pass = true;
            } else {
                auto_fail = true;
                note_str = "No authenticated WS clients observed";
            }
        } else if (strcmp(test_id, "REC-BACKUP-001") == 0) {
            if (probe.backup_serializer_healthy) {
                auto_pass = true;
            } else {
                auto_blocked = true;
                note_str = "Backup serializer verification failed";
            }
        } else {
            auto_blocked = true;
            note_str = "Automated probe pending instrument/fixture support";
        }

        if (auto_pass) {
            res.state = CertificationTestState::Passed;
            res.completed_ms = esp_timer_get_time() / 1000ULL;
            if (note_str) strncpy(res.note, note_str, sizeof(res.note) - 1);
            current_test_[0] = '\0';
            evaluate_overall_state();
            return persist_record();
        } else if (auto_fail) {
            res.state = CertificationTestState::Failed;
            res.completed_ms = esp_timer_get_time() / 1000ULL;
            res.error_code = -1;
            if (note_str) strncpy(res.note, note_str, sizeof(res.note) - 1);
            current_test_[0] = '\0';
            evaluate_overall_state();
            return persist_record();
        } else if (auto_blocked) {
            res.state = CertificationTestState::Blocked;
            res.completed_ms = esp_timer_get_time() / 1000ULL;
            res.error_code = -1;
            if (note_str) strncpy(res.note, note_str, sizeof(res.note) - 1);
            current_test_[0] = '\0';
            evaluate_overall_state();
            return persist_record();
        }
    } else {
        // Instrument-only and external observation tests have no local output
        // sequence. They still need the bounded operator-evidence timeout.
        res.state = CertificationTestState::WaitingForOperator;
    }

    if (res.state == CertificationTestState::Blocked || res.state == CertificationTestState::Skipped) {
        res.completed_ms = esp_timer_get_time() / 1000ULL;
        current_test_[0] = '\0';
        evaluate_overall_state();
    }
    return persist_record();
}

esp_err_t CertificationService::record_result(const char* test_id,
                                              CertificationTestState state,
                                              EvidenceSource source,
                                              float measured_value,
                                              const char* unit,
                                              const char* note) {
    if (!is_in_progress() || cancellation_pending_ || confirmation_pending_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (source > EvidenceSource::InstrumentMeasurement ||
        (state != CertificationTestState::Passed && state != CertificationTestState::Failed &&
         state != CertificationTestState::Skipped && state != CertificationTestState::Blocked) ||
        !std::isfinite(measured_value)) {
        return ESP_ERR_INVALID_ARG;
    }

    const auto* def = CertificationPlan::find_test(test_id);
    if (!def) {
        return ESP_ERR_NOT_FOUND;
    }

    int idx = find_result_index(test_id);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    CertificationRecord& rec = store_.current_record();
    CertificationTestResult& res = rec.results[idx];

    // Physical observations and instrument measurements cannot be replaced by
    // weaker evidence (including an Automatic label supplied by a client).
    if (def->required_evidence == EvidenceSource::Automatic || source < def->required_evidence ||
        (unit && strlen(unit) >= sizeof(res.unit)) || (note && strlen(note) >= sizeof(res.note))) {
        ESP_LOGE(TAG, "Rejected: Test %s requires stronger evidence", test_id);
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(current_test_, test_id) != 0 || res.state != CertificationTestState::WaitingForOperator) {
        // An observation must belong to the executing test. Never create
        // evidence for an unstarted test or overwrite a completed attempt.
        return ESP_ERR_INVALID_STATE;
    }

    // Spec §86, §87, §112 M, N, O: For physical tests, verify active FactoryRunner session
    const bool physical_test = strncmp(test_id, "ADDR-A", 6) == 0 || strncmp(test_id, "PWM-P", 5) == 0 ||
        strcmp(test_id, "ADDR-XTALK") == 0 || strcmp(test_id, "PWM-XTALK") == 0 ||
        strcmp(test_id, "SAFE-ESTOP-ADDRESSABLE") == 0 || strcmp(test_id, "SAFE-ESTOP-PWM") == 0 ||
        strcmp(test_id, "SAFE-DISABLE-001") == 0;
    if (physical_test) {
        if (!runner_ || current_factory_session_id_ == 0) return ESP_ERR_INVALID_STATE;
        const FactoryStatus fstat = runner_->status();
        if (fstat.session.owner != FactorySessionOwner::Certification ||
            strcmp(fstat.session.certification_test_id, test_id) != 0 ||
            fstat.session.session_id != current_factory_session_id_ ||
            fstat.session.state != FactoryTestState::WaitingForOperator) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    // Spec §43, §113 T, U, V: Instrument measurement validation
    CertificationTestState final_state = state;
    if (def->required_evidence == EvidenceSource::InstrumentMeasurement &&
        (state == CertificationTestState::Passed || state == CertificationTestState::Failed)) {
        if (!unit || strcmp(unit, def->unit) != 0) return ESP_ERR_INVALID_ARG;
        // Never turn an explicitly reported failure into a pass merely because
        // one recorded numeric sample happens to be inside the tolerance.
        if (state == CertificationTestState::Passed && def->tolerance > 0.0f &&
            std::fabs(measured_value - def->expected_value) > def->tolerance) {
            final_state = CertificationTestState::Failed;
        }
    }

    // Spec §25, §76, §120 F: Failure note mandatory
    if (final_state == CertificationTestState::Failed) {
        if (!note || note[0] == '\0') {
            ESP_LOGE(TAG, "Rejected: Failure note is mandatory on failed certification test %s", test_id);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Queue the owned output confirmation before accepting its observation.
    // A full queue is not a recorded pass and leaves this attempt open.
    if (physical_test && !runner_->confirm_test_session(
            current_factory_session_id_, final_state == CertificationTestState::Passed, note)) {
        return ESP_ERR_INVALID_STATE;
    }

    // Apply result
    // Persist an unfinished execution until its output stop is acknowledged.
    // If the node reboots here, this attempt must not reappear as passed.
    res.state = physical_test ? CertificationTestState::Running : final_state;
    res.evidence_source = source;
    res.completed_ms = physical_test ? 0 : esp_timer_get_time() / 1000ULL;
    res.measured_value = measured_value;
    snprintf(res.unit, sizeof(res.unit), "%s", unit ? unit : "");
    snprintf(res.note, sizeof(res.note), "%s", note ? note : "");

    if (physical_test) {
        confirmation_pending_ = true;
        confirmation_result_state_ = final_state;
    } else {
        current_test_[0] = '\0';
        evaluate_overall_state();
    }
    return persist_record();
}

esp_err_t CertificationService::persist_record() {
    const esp_err_t err = store_.save(store_.current_record());
    if (err == ESP_OK) return ESP_OK;

    // An in-memory result is not durable evidence. Keep the result available
    // for export, but stop the session and expose why it cannot be accepted.
    auto& rec = store_.current_record();
    rec.session.state = CertificationState::Incomplete;
    rec.session.completed_ms = esp_timer_get_time() / 1000ULL;
    is_stale_ = true;
    snprintf(stale_reason_, sizeof(stale_reason_), "persistence_failed");
    confirmation_pending_ = false;
    if (!cancellation_pending_) {
        const esp_err_t cancel_error = request_output_cancellation();
        if (cancel_error != ESP_OK) ESP_LOGE(TAG, "Certification cancellation was not queued; tick will retry");
    }
    if (!cancellation_pending_) current_test_[0] = '\0';
    ESP_LOGE(TAG, "Certification evidence not persisted: %s", esp_err_to_name(err));
    return err;
}

void CertificationService::evaluate_overall_state() {
    CertificationRecord& rec = store_.current_record();
    uint16_t passed_count = 0;
    uint16_t failed_count = 0;
    uint16_t skipped_count = 0;

    bool has_pending_required = false;
    bool has_failed_required = false;
    bool has_incomplete_required = rec.result_count > kMaxCertificationTests ||
        rec.result_count != CertificationPlan::test_count() ||
        rec.session.test_count != rec.result_count;

    for (size_t i = 0; i < rec.result_count && i < kMaxCertificationTests; ++i) {
        const auto& r = rec.results[i];

        if (r.state == CertificationTestState::Passed) {
            passed_count++;
        } else if (r.state == CertificationTestState::Failed) {
            failed_count++;
        } else if (r.state == CertificationTestState::Skipped) {
            skipped_count++;
        }
    }

    // Walk the required plan, not only the records that happen to be present.
    // Missing, skipped and blocked evidence can never certify the session.
    const auto* definitions = CertificationPlan::test_definitions();
    for (size_t i = 0; i < CertificationPlan::test_count(); ++i) {
        if (definitions[i].requirement_for_profile(rec.session.profile) != RequirementLevel::Required) continue;
        const int index = find_result_index(definitions[i].id);
        if (index < 0) {
            has_incomplete_required = true;
            continue;
        }
        switch (rec.results[index].state) {
        case CertificationTestState::Passed: break;
        case CertificationTestState::Failed: has_failed_required = true; break;
        case CertificationTestState::Pending:
        case CertificationTestState::Running:
        case CertificationTestState::WaitingForOperator: has_pending_required = true; break;
        default: has_incomplete_required = true; break;
        }
    }

    rec.session.passed_count = passed_count;
    rec.session.failed_count = failed_count;
    rec.session.skipped_count = skipped_count;

    if (has_failed_required) {
        rec.session.state = CertificationState::Failed;
        rec.session.completed_ms = esp_timer_get_time() / 1000ULL;
    } else if (has_pending_required) {
        rec.session.state = CertificationState::InProgress;
        rec.session.completed_ms = 0;
    } else if (has_incomplete_required) {
        rec.session.state = CertificationState::Incomplete;
        rec.session.completed_ms = esp_timer_get_time() / 1000ULL;
    } else {
        rec.session.state = CertificationState::Passed;
        rec.session.completed_ms = esp_timer_get_time() / 1000ULL;
    }
}

esp_err_t CertificationService::request_output_cancellation() {
    FactoryStatus status{};
    if (runner_) status = runner_->status();
    if (!cancellation_pending_) {
        const bool owns_active_session = status.session.owner == FactorySessionOwner::Certification &&
            (status.session.state == FactoryTestState::Preparing ||
             status.session.state == FactoryTestState::Running ||
             status.session.state == FactoryTestState::WaitingForOperator);
        if (!burn_in_active_) {
            // A completed/unqueued test can leave its old ID behind. Keep an
            // accepted start that the worker has not reached yet, but do not
            // manufacture pending work for an already finished test.
            if (!owns_active_session && current_test_[0] == '\0') current_factory_session_id_ = 0;
            if (owns_active_session && current_factory_session_id_ == 0)
                current_factory_session_id_ = status.session.session_id;
        }
        cancellation_pending_ = burn_in_active_ || current_factory_session_id_ != 0;
        cancellation_queued_ = false;
        if (!cancellation_pending_) return ESP_OK;
    }

    bool acknowledged = false;
    if (burn_in_active_) {
        OperationJob job{};
        acknowledged = job_mgr_ && burn_in_job_id_ != 0 && job_mgr_->get_job(burn_in_job_id_, job) &&
            (job.state == JobState::Completed || job.state == JobState::Failed || job.state == JobState::Cancelled);
    } else {
        acknowledged = runner_ && status.session.owner == FactorySessionOwner::Certification &&
            status.session.session_id == current_factory_session_id_ &&
            !status.outputs_enabled && !status.test_active &&
            (status.session.state == FactoryTestState::Passed || status.session.state == FactoryTestState::Failed ||
             status.session.state == FactoryTestState::Aborted);
    }
    if (acknowledged) {
        // Job completion follows its output operation. Physical sessions also
        // need inactive output flags because confirmation publishes its state
        // before blackout completes. Failed blackout stays a failed job.
        cancellation_pending_ = false;
        cancellation_queued_ = false;
        burn_in_active_ = false;
        burn_in_job_id_ = 0;
        current_factory_session_id_ = 0;
        current_test_[0] = '\0';
        return ESP_OK;
    }
    if (!runner_) return ESP_ERR_INVALID_STATE;
    if (!cancellation_queued_) {
        // Scope cancellation to the owned session; a delayed abort must not
        // stop a subsequent manual factory job. Retry only a rejected enqueue.
        cancellation_queued_ = burn_in_active_ ? runner_->stop_burn_in_job() :
            runner_->abort_test_session(current_factory_session_id_);
        if (!cancellation_queued_) return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t CertificationService::abort() {
    confirmation_pending_ = false;
    if (current_test_[0] != '\0') {
        int idx = find_result_index(current_test_);
        if (idx >= 0) {
            CertificationRecord& rec = store_.current_record();
            rec.results[idx].state = CertificationTestState::Blocked;
            strncpy(rec.results[idx].note, "abort requested", sizeof(rec.results[idx].note) - 1);
            rec.results[idx].completed_ms = esp_timer_get_time() / 1000ULL;
        }
    }

    const esp_err_t cancel_error = request_output_cancellation();
    if (!cancellation_pending_) current_test_[0] = '\0';

    CertificationRecord& rec = store_.current_record();
    if (rec.session.state == CertificationState::InProgress) {
        rec.session.state = CertificationState::Incomplete;
        rec.session.completed_ms = esp_timer_get_time() / 1000ULL;
    }

    const esp_err_t persist_error = persist_record();
    return persist_error != ESP_OK ? persist_error : cancel_error;
}

void CertificationService::check_invalidation(const DeviceConfig& current_cfg, const char* current_hw_rev, const char* current_fw_ver) {
    CertificationRecord& rec = store_.current_record();
    if (rec.session.state == CertificationState::NotStarted) {
        is_stale_ = false;
        stale_reason_[0] = '\0';
        return;
    }

    // 1. Hardware revision change -> Invalidate
    if (current_hw_rev && rec.session.identity.hardware_revision[0] != '\0' &&
        strcmp(rec.session.identity.hardware_revision, current_hw_rev) != 0) {
        rec.session.state = CertificationState::Invalidated;
        is_stale_ = true;
        snprintf(stale_reason_, sizeof(stale_reason_), "hardware_revision_changed");
        persist_record();
        return;
    }

    // 2. Plan version change -> Invalidate
    if (rec.session.identity.plan_version != kCertificationPlanVersion) {
        rec.session.state = CertificationState::Invalidated;
        is_stale_ = true;
        snprintf(stale_reason_, sizeof(stale_reason_), "plan_version_changed");
        persist_record();
        return;
    }

    // 3. Relevant hardware config change -> Invalidate
    uint32_t current_fp = compute_hardware_fingerprint(current_cfg, current_hw_rev);
    if (rec.session.config_fingerprint != 0 && rec.session.config_fingerprint != current_fp) {
        rec.session.state = CertificationState::Invalidated;
        is_stale_ = true;
        snprintf(stale_reason_, sizeof(stale_reason_), "hardware_config_changed");
        persist_record();
        return;
    }

    // 4. Firmware version change -> Mark Stale
    if (current_fw_ver && rec.session.identity.firmware_version[0] != '\0' &&
        strcmp(rec.session.identity.firmware_version, current_fw_ver) != 0) {
        is_stale_ = true;
        snprintf(stale_reason_, sizeof(stale_reason_), "firmware_changed");
        return;
    }

    is_stale_ = false;
    stale_reason_[0] = '\0';
}

void CertificationService::update_burn_in_tick(uint32_t elapsed_s, uint32_t commit_fails, uint32_t output_faults) {
    if (!burn_in_active_) return;
    CertificationRecord& rec = store_.current_record();
    rec.burn_in.duration_s = elapsed_s;
    rec.burn_in.commit_failures = commit_fails;
    rec.burn_in.output_faults = output_faults;

    uint32_t heap = static_cast<uint32_t>(esp_get_free_heap_size());
    if (heap < rec.burn_in.min_free_heap) {
        rec.burn_in.min_free_heap = heap;
    }
    // LG-038.3 §74: 8-bit heap truth minima over the run.
    const RuntimeMemorySnapshot mem = capture_runtime_memory();
    if (burn_in_min_free8_ == 0 || mem.free_8bit < burn_in_min_free8_) {
        burn_in_min_free8_ = mem.free_8bit;
    }
    if (burn_in_min_largest8_ == 0 || mem.largest_8bit < burn_in_min_largest8_) {
        burn_in_min_largest8_ = mem.largest_8bit;
    }
}

void CertificationService::finish_burn_in(bool passed, const char* reason) {
    if (!burn_in_active_ || cancellation_pending_) return;
    update_burn_in_tick(store_.current_record().burn_in.duration_s, store_.current_record().burn_in.commit_failures, store_.current_record().burn_in.output_faults);
    const auto mem = capture_runtime_memory();
    burn_in_end_free8_ = mem.free_8bit;
    burn_in_end_largest8_ = mem.largest_8bit;
    burn_in_active_ = false;
    CertificationRecord& rec = store_.current_record();
    rec.burn_in.end_free_heap = static_cast<uint32_t>(esp_get_free_heap_size());

    record_result("BURN-001",
                  passed ? CertificationTestState::Passed : CertificationTestState::Failed,
                  EvidenceSource::Automatic,
                  static_cast<float>(rec.burn_in.duration_s),
                  "seconds",
                  reason ? reason : (passed ? "completed" : "failed"));
}

void CertificationService::tick(uint64_t now_ms) {
    if (confirmation_pending_ && (!is_in_progress() || is_stale_)) {
        // A configuration/image invalidation supersedes a submitted result.
        // Still stop the owned outputs, but a late success cannot re-certify
        // evidence whose enclosing session is no longer current.
        confirmation_pending_ = false;
        auto& record = store_.current_record();
        const int index = find_result_index(current_test_);
        if (index >= 0) {
            record.results[index].state = CertificationTestState::Blocked;
            record.results[index].completed_ms = now_ms;
            record.results[index].error_code = ESP_ERR_INVALID_STATE;
        }
        if (record.session.state == CertificationState::InProgress) {
            record.session.state = CertificationState::Incomplete;
            record.session.completed_ms = now_ms;
        }
        request_output_cancellation();
        persist_record();
        return;
    }
    // Persistence failure and explicit abort make evidence Incomplete before
    // the worker stops. Continue cancellation retry/acknowledgement regardless
    // of that evidence state, retaining ownership until the worker responds.
    if (cancellation_pending_) {
        request_output_cancellation();
        return;
    }
    if (confirmation_pending_) {
        const int index = find_result_index(current_test_);
        if (!runner_ || index < 0) return;
        const FactoryStatus status = runner_->status();
        if (status.session.owner != FactorySessionOwner::Certification ||
            status.session.session_id != current_factory_session_id_ ||
            strcmp(status.session.certification_test_id, current_test_) != 0 ||
            status.outputs_enabled || status.test_active ||
            (status.session.state != FactoryTestState::Passed &&
             status.session.state != FactoryTestState::Failed &&
             status.session.state != FactoryTestState::Aborted)) return;

        auto& result = store_.current_record().results[index];
        result.state = confirmation_result_state_;
        result.completed_ms = now_ms;
        result.error_code = status.session.error_code;
        if (status.session.state == FactoryTestState::Aborted) {
            result.state = CertificationTestState::Blocked;
        } else if (status.session.error_code != ESP_OK ||
                   (status.session.state == FactoryTestState::Passed) !=
                       (confirmation_result_state_ == CertificationTestState::Passed)) {
            result.state = CertificationTestState::Failed;
            if (result.error_code == ESP_OK) result.error_code = ESP_ERR_INVALID_STATE;
        }
        // Keep the submitted method/observation intact; the runner error code
        // records a failed stop without truncating or replacing that evidence.
        confirmation_pending_ = false;
        current_factory_session_id_ = 0;
        current_test_[0] = '\0';
        evaluate_overall_state();
        persist_record();
        return;
    }
    if (!is_in_progress()) return;

    CertificationRecord& rec = store_.current_record();

    // 1. Operator evidence timeout (Spec §64: 5 minutes)
    if (current_test_[0] != '\0') {
        int idx = find_result_index(current_test_);
        if (idx >= 0) {
            CertificationTestResult& res = rec.results[idx];
            if (res.state == CertificationTestState::WaitingForOperator) {
                if (now_ms - current_test_started_ms_ > 300000ULL) {
                    ESP_LOGW(TAG, "Operator evidence timed out for %s (5 minutes elapsed)", current_test_);
                    res.state = CertificationTestState::Blocked;
                    res.completed_ms = now_ms;
                    res.error_code = -2;
                    snprintf(res.note, sizeof(res.note), "Operator observation timed out after 5m");
                    request_output_cancellation();
                    if (!cancellation_pending_) current_test_[0] = '\0';
                    evaluate_overall_state();
                    persist_record();
                    return;
                }
            } else if (res.state == CertificationTestState::Running && runner_) {
                // Check if factory test transitioned to WaitingForOperator or Failed
                FactoryStatus fstat = runner_->status();
                if (fstat.session.owner == FactorySessionOwner::Certification &&
                    strcmp(fstat.session.certification_test_id, current_test_) == 0) {
                    if (fstat.session.state == FactoryTestState::WaitingForOperator) {
                        res.state = CertificationTestState::WaitingForOperator;
                        persist_record();
                    } else if (fstat.session.state == FactoryTestState::Failed) {
                        res.state = CertificationTestState::Failed;
                        res.completed_ms = now_ms;
                        res.error_code = -1;
                        snprintf(res.note, sizeof(res.note), "Factory sequence failed");
                        current_test_[0] = '\0';
                        evaluate_overall_state();
                        persist_record();
                        return;
                    }
                }
            }
        }
    }

    // 2. Burn-In Job state tracking (LG-038 §61-§63)
    if (burn_in_active_ && job_mgr_) {
        update_burn_in_tick(static_cast<uint32_t>((now_ms - burn_in_start_ms_) / 1000), rec.burn_in.commit_failures, rec.burn_in.output_faults);
        OperationJob job{};
        if (job_mgr_->get_job(burn_in_job_id_, job)) {
            int burn_idx = find_result_index("BURN-001");
            if (burn_idx >= 0) {
                CertificationTestResult& bres = rec.results[burn_idx];
                if (job.state == JobState::Running) {
                    if (runner_) {
                        auto st = runner_->status();
                        auto rpt = runner_->validation_report();
                        rec.burn_in.commit_failures = st.commit_failures;
                        rec.burn_in.output_faults = rpt.burn_in_errors;
                        rec.burn_in.duration_s = static_cast<uint32_t>((now_ms - burn_in_start_ms_) / 1000);
                        uint32_t heap = static_cast<uint32_t>(esp_get_free_heap_size());
                        if (heap < rec.burn_in.min_free_heap) {
                            rec.burn_in.min_free_heap = heap;
                        }
                    }
                } else if (job.state == JobState::Completed) {
                    uint32_t commit_fails = 0;
                    uint32_t out_faults = 0;
                    if (runner_) {
                        auto st = runner_->status();
                        auto rpt = runner_->validation_report();
                        commit_fails = st.commit_failures;
                        out_faults = rpt.burn_in_errors;
                        rec.burn_in.commit_failures = commit_fails;
                        rec.burn_in.output_faults = out_faults;
                        rec.burn_in.duration_s = static_cast<uint32_t>((now_ms - burn_in_start_ms_) / 1000);
                        rec.burn_in.end_free_heap = static_cast<uint32_t>(esp_get_free_heap_size());
                    }
                    bool pass = (job.error_code == 0 && commit_fails == 0 && out_faults == 0);
                    bres.state = pass ? CertificationTestState::Passed : CertificationTestState::Failed;
                    bres.completed_ms = now_ms;
                    bres.error_code = pass ? 0 : -1;
                    if (!pass) {
                        snprintf(bres.note, sizeof(bres.note), "Burn-in fault: fails=%lu faults=%lu err=%ld",
                                 (unsigned long)commit_fails, (unsigned long)out_faults, (long)job.error_code);
                    }
                    const auto end_mem = capture_runtime_memory();
                    burn_in_end_free8_ = end_mem.free_8bit;
                    burn_in_end_largest8_ = end_mem.largest_8bit;
                    burn_in_active_ = false;
                    if (strcmp(current_test_, "BURN-001") == 0) {
                        current_test_[0] = '\0';
                    }
                    evaluate_overall_state();
                    persist_record();
                } else if (job.state == JobState::Failed) {
                    bres.state = CertificationTestState::Failed;
                    bres.completed_ms = now_ms;
                    bres.error_code = job.error_code;
                    snprintf(bres.note, sizeof(bres.note), "Burn-in job failed (%ld)", (long)job.error_code);
                    const auto end_mem = capture_runtime_memory();
                    burn_in_end_free8_ = end_mem.free_8bit;
                    burn_in_end_largest8_ = end_mem.largest_8bit;
                    burn_in_active_ = false;
                    if (strcmp(current_test_, "BURN-001") == 0) {
                        current_test_[0] = '\0';
                    }
                    evaluate_overall_state();
                    persist_record();
                } else if (job.state == JobState::Cancelled) {
                    bres.state = CertificationTestState::Blocked;
                    bres.completed_ms = now_ms;
                    snprintf(bres.note, sizeof(bres.note), "Burn-in job cancelled");
                    const auto end_mem = capture_runtime_memory();
                    burn_in_end_free8_ = end_mem.free_8bit;
                    burn_in_end_largest8_ = end_mem.largest_8bit;
                    burn_in_active_ = false;
                    if (strcmp(current_test_, "BURN-001") == 0) {
                        current_test_[0] = '\0';
                    }
                    evaluate_overall_state();
                    persist_record();
                }
            }
        }
    }
}

CertificationStatusSummary CertificationService::get_status_summary() const {
    const auto& session_info = store_.current_record().session;
    CertificationStatusSummary summary{};
    summary.state = session_info.state;
    strncpy(summary.current_test, current_test_, sizeof(summary.current_test) - 1);
    summary.passed = session_info.passed_count;
    summary.failed = session_info.failed_count;
    summary.total = session_info.test_count;
    summary.is_stale = is_stale_;
    strncpy(summary.stale_reason, stale_reason_, sizeof(summary.stale_reason) - 1);
    return summary;
}

cJSON* CertificationService::generate_report_result_json(size_t index) const {
    const auto& rec = store_.current_record();
    if (index >= rec.result_count || index >= kMaxCertificationTests) return nullptr;
    const auto& r = rec.results[index];
    const auto* def = CertificationPlan::find_test(r.test_id);
    cJSON* t_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(t_obj, "id", r.test_id);
    if (def) {
        cJSON_AddStringToObject(t_obj, "category", def->category);
        cJSON_AddStringToObject(t_obj, "title", def->title);
        cJSON_AddStringToObject(t_obj, "requirement", requirement_level_to_string(def->requirement_for_profile(rec.session.profile)));
    }
    cJSON_AddStringToObject(t_obj, "state", cert_test_state_to_string(r.state));
    cJSON_AddStringToObject(t_obj, "evidenceSource", evidence_source_to_string(r.evidence_source));
    cJSON_AddNumberToObject(t_obj, "startedMs", static_cast<double>(r.started_ms));
    cJSON_AddNumberToObject(t_obj, "completedMs", static_cast<double>(r.completed_ms));
    cJSON_AddNumberToObject(t_obj, "errorCode", r.error_code);
    cJSON_AddNumberToObject(t_obj, "measuredValue", r.measured_value);
    cJSON_AddStringToObject(t_obj, "unit", r.unit);
    cJSON_AddStringToObject(t_obj, "note", r.note);
    cJSON_AddNumberToObject(t_obj, "attempts", r.attempts);
    return t_obj;
}

cJSON* CertificationService::generate_report_json(bool include_results) const {
    const auto& rec = store_.current_record();
    cJSON* root = cJSON_CreateObject();
    if (!root) return nullptr;

    // Spec §124: SECRET INVARIANT: Never include credentials, Wi-Fi password, session tokens, sync tokens!
    cJSON_AddStringToObject(root, "schema", "lumigrid-certification-v1");
    cJSON_AddStringToObject(root, "certificationId", rec.session.identity.certification_id);
    cJSON_AddStringToObject(root, "nodeId", rec.session.identity.node_id);
    cJSON_AddStringToObject(root, "serialNumber", rec.session.identity.serial_number);
    cJSON_AddStringToObject(root, "firmwareVersion", rec.session.identity.firmware_version);
    cJSON_AddStringToObject(root, "hardwareRevision", rec.session.identity.hardware_revision);
    cJSON_AddStringToObject(root, "projectName", rec.session.identity.project_name);
    cJSON_AddNumberToObject(root, "planVersion", rec.session.identity.plan_version);
    // This identifies the image exporting the report, not an older session's
    // image. Record layout v2 deliberately does not reinterpret old evidence.
    if (const auto* app = esp_app_get_description()) {
        char hash[65]{};
        constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) {
            hash[2 * i] = hex[app->app_elf_sha256[i] >> 4];
            hash[2 * i + 1] = hex[app->app_elf_sha256[i] & 0x0f];
        }
        cJSON_AddStringToObject(root, "reportingFirmwareElfSha256", hash);
        cJSON_AddStringToObject(root, "reportingFirmwareVersion", app->version);
    }

    cJSON_AddStringToObject(root, "profile", certification_profile_to_string(rec.session.profile));
    cJSON_AddStringToObject(root, "state", certification_state_to_string(rec.session.state));
    cJSON_AddBoolToObject(root, "stale", is_stale_);
    if (is_stale_) {
        cJSON_AddStringToObject(root, "staleReason", stale_reason_);
    }

    cJSON_AddNumberToObject(root, "startedMs", static_cast<double>(rec.session.started_ms));
    cJSON_AddNumberToObject(root, "completedMs", static_cast<double>(rec.session.completed_ms));
    cJSON_AddNumberToObject(root, "passedCount", rec.session.passed_count);
    cJSON_AddNumberToObject(root, "failedCount", rec.session.failed_count);
    cJSON_AddNumberToObject(root, "skippedCount", rec.session.skipped_count);
    cJSON_AddNumberToObject(root, "testCount", rec.session.test_count);
    cJSON_AddNumberToObject(root, "configFingerprint", rec.session.config_fingerprint);

    cJSON* summary_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(summary_obj, "state", certification_state_to_string(rec.session.state));
    cJSON_AddNumberToObject(summary_obj, "passed", rec.session.passed_count);
    cJSON_AddNumberToObject(summary_obj, "failed", rec.session.failed_count);
    cJSON_AddNumberToObject(summary_obj, "skipped", rec.session.skipped_count);
    cJSON_AddNumberToObject(summary_obj, "total", rec.session.test_count);
    cJSON_AddBoolToObject(summary_obj, "isStale", is_stale_);
    cJSON_AddItemToObject(root, "summary", summary_obj);

    // Tests array
    cJSON* tests_arr = cJSON_CreateArray();
    if (include_results) {
        for (size_t i = 0; i < rec.result_count && i < kMaxCertificationTests; ++i)
            cJSON_AddItemToArray(tests_arr, generate_report_result_json(i));
    }
    cJSON_AddItemToObject(root, "tests", tests_arr);

    // Burn-in metrics
    cJSON* burn_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(burn_obj, "durationS", rec.burn_in.duration_s);
    cJSON_AddNumberToObject(burn_obj, "commitFailures", rec.burn_in.commit_failures);
    cJSON_AddNumberToObject(burn_obj, "outputFaults", rec.burn_in.output_faults);
    cJSON_AddNumberToObject(burn_obj, "startFreeHeap", rec.burn_in.start_free_heap);
    cJSON_AddNumberToObject(burn_obj, "minFreeHeap", rec.burn_in.min_free_heap);
    cJSON_AddNumberToObject(burn_obj, "endFreeHeap", rec.burn_in.end_free_heap);
    // LG-038.3 §74: canonical 8-bit heap evidence.
    cJSON_AddNumberToObject(burn_obj, "startFree8", burn_in_start_free8_);
    cJSON_AddNumberToObject(burn_obj, "minFree8", burn_in_min_free8_);
    cJSON_AddNumberToObject(burn_obj, "endFree8", burn_in_end_free8_);
    cJSON_AddNumberToObject(burn_obj, "startLargest8", burn_in_start_largest8_);
    cJSON_AddNumberToObject(burn_obj, "minLargest8", burn_in_min_largest8_);
    cJSON_AddNumberToObject(burn_obj, "endLargest8", burn_in_end_largest8_);
    cJSON_AddNumberToObject(burn_obj, "networkDisconnects", rec.burn_in.network_disconnects);
    cJSON_AddNumberToObject(burn_obj, "watchdogResets", rec.burn_in.watchdog_resets);
    cJSON_AddItemToObject(root, "burnIn", burn_obj);

    return root;
}

} // namespace lg
