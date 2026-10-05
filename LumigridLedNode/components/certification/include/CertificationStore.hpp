// components/certification/include/CertificationStore.hpp
#pragma once

#include "CertificationTypes.hpp"
#include "CertificationPlan.hpp"
#include "esp_err.h"

namespace lg {

struct CertificationRecord {
    uint32_t magic = 0x4C474354; // "LGCT"
    uint16_t schema_version = 1;
    uint16_t reserved = 0;
    uint32_t sequence = 0;
    CertificationSession session{};
    uint16_t result_count = 0;
    CertificationTestResult results[kMaxCertificationTests]{};
    BurnInMetrics burn_in{};
    uint32_t checksum = 0;
};

class CertificationStore {
public:
    CertificationStore() = default;
    ~CertificationStore() = default;

    esp_err_t init();
    esp_err_t load(CertificationRecord& record);
    esp_err_t save(const CertificationRecord& record);
    esp_err_t erase();

    bool is_initialized() const { return initialized_; }
    const CertificationRecord& current_record() const { return record_; }
    CertificationRecord& current_record() { return record_; }

    static uint32_t calculate_crc(const CertificationRecord& record);

private:
    bool initialized_ = false;
    CertificationRecord record_{};
};

} // namespace lg
