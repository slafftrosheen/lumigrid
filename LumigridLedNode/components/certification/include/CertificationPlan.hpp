// components/certification/include/CertificationPlan.hpp
#pragma once

#include "CertificationTypes.hpp"

namespace lg {

static constexpr uint16_t kCertificationPlanVersion = 1;
static constexpr size_t kMaxCertificationTests = 40;

class CertificationPlan {
public:
    static size_t test_count();
    static const CertificationTestDefinition* test_definitions();
    static const CertificationTestDefinition* find_test(const char* test_id);
    static int find_test_index(const char* test_id);

    static RequirementLevel get_requirement(const char* test_id, CertificationProfile profile);
    static uint32_t get_timeout_ms(const char* test_id);
    static EvidenceSource get_required_evidence(const char* test_id);
};

} // namespace lg
