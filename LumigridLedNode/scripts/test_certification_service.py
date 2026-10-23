"""Compile the real certification plan/service against bounded host fakes.

The service, plan, result buffers and report serializer are production code.
NVS durability has its own fake-I/O tests; this harness injects save/config
failures and does not claim any hardware, RF or CE measurement was performed.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = {
    "esp_app_desc.h": r'''
#pragma once
#include <cstdint>
struct esp_app_desc_t { char version[32]="reporting-image"; uint8_t app_elf_sha256[32]{0xa5}; };
inline const esp_app_desc_t* esp_app_get_description() { static esp_app_desc_t app; return &app; }
''',
    "esp_err.h": r'''
#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101,
    ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
    ESP_ERR_INVALID_SIZE=0x104, ESP_ERR_NOT_FOUND=0x105;
inline const char* esp_err_to_name(esp_err_t) { return "fake_error"; }
''',
    "esp_log.h": r'''
#pragma once
#include <inttypes.h>
template<class... Args> inline void fake_log(Args&&...) {}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
''',
    "esp_timer.h": r'''
#pragma once
#include <cstdint>
inline int64_t fake_clock_us=1000000;
inline int64_t esp_timer_get_time() { return fake_clock_us; }
inline uint32_t esp_get_free_heap_size() { return 100000; }
''',
    "esp_rom_crc.h": r'''
#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* data, size_t size) {
    for (size_t i=0; i<size; ++i) crc = (crc * 33) ^ data[i];
    return crc;
}
''',
    "NodeIdentity.hpp": r'''
#pragma once
namespace lg { struct NodeIdentity {
    char id[24]="lg-test", fw_version[32]="1.0.0-test", hw_revision[16]="A";
}; }
''',
    "SignageTypes.hpp": r'''
#pragma once
#include <cstdint>
#include <cstddef>
namespace lg {
constexpr size_t kMaxPowerSupplies=4;
struct Channel { bool enabled=true; char name[32]="channel";
    uint8_t addressable_type=0, pixel_order=0; uint16_t pixel_count=32;
    uint32_t max_milliwatts=5000, rated_full_power_mw=5000, channel_limit_mw=5000;
};
struct DeviceConfig {
    char serial_number[32]=""; Channel channels[16];
    uint32_t max_power_mw=50000; uint32_t power_supplies[kMaxPowerSupplies]{};
};
}
''',
    "ConfigStore.hpp": r'''
#pragma once
#include "SignageTypes.hpp"
#include "esp_err.h"
namespace lg {
inline int config_error=ESP_OK;
class ConfigStore { public: int load_config(DeviceConfig&) { return config_error; } };
}
''',
    "FactoryRunner.hpp": r'''
#pragma once
#include "FactoryTypes.hpp"
namespace lg {
class JobManager;
class FactoryRunner { public:
    static constexpr uint8_t kBurnInDefaultBrightness=32;
    FactoryStatus value{}; int aborts=0;
    FactoryStatus status() const { return value; }
    FactoryValidationReport validation_report() const { return {}; }
    bool start_test_session(FactoryTestKind, uint8_t, uint32_t, uint8_t,
        uint32_t*, FactorySessionOwner, const char*) { return false; }
    bool start_burn_in_job(JobManager*, uint32_t, uint8_t, uint32_t*) { return false; }
    bool confirm_test_session(uint32_t, bool, const char*) { return true; }
    bool abort() { ++aborts; return true; }
};
}
''',
    "JobManager.hpp": r'''
#pragma once
#include <cstdint>
namespace lg {
enum class JobState { Running, Completed, Failed, Cancelled };
struct OperationJob { JobState state=JobState::Running; int32_t error_code=0; };
class JobManager { public:
    int cancellations=0; uint32_t cancelled_job=0;
    bool get_job(uint32_t, OperationJob&) const { return false; }
    void cancel_job(uint32_t id) { ++cancellations; cancelled_job=id; }
};
}
''',
    "RuntimeMemory.hpp": r'''
#pragma once
#include <cstdint>
namespace lg {
struct RuntimeMemorySnapshot { uint32_t free_8bit=100000, largest_8bit=50000; };
inline RuntimeMemorySnapshot capture_runtime_memory() { return {}; }
}
''',
}

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <utility>
#include <iostream>
#include "ConfigStore.hpp"
#include "FactoryRunner.hpp"
#include "JobManager.hpp"
#include "esp_timer.h"
#include "cJSON.h"
#define private public
#include "CertificationService.hpp"
#undef private
namespace lg {
inline int save_error=ESP_OK;
int CertificationStore::init() { initialized_=true; return ESP_OK; }
int CertificationStore::save(const CertificationRecord& record) {
    if (save_error != ESP_OK) return save_error;
    record_=record; initialized_=true; return ESP_OK;
}
}
using namespace lg;

void seed(CertificationService& service, const char* test_id="POWER-LIMIT-001") {
    service.store_.init();
    auto& record=service.store_.current_record();
    record.session.state=CertificationState::InProgress;
    record.session.profile=CertificationProfile::ProductionUnit;
    record.session.identity.plan_version=kCertificationPlanVersion;
    record.result_count=1; record.session.test_count=1;
    std::strncpy(record.results[0].test_id,test_id,sizeof(record.results[0].test_id)-1);
}

int main(int argc, char** argv) {
    assert(argc==2);
    const std::string name=argv[1];
    CertificationService service;
    if(name=="plan_bounds") {
        assert(CertificationPlan::test_count()<=kMaxCertificationTests);
        const auto* tests=CertificationPlan::test_definitions();
        FactoryCommand command; FactoryTestSession session;
        for(size_t i=0;i<CertificationPlan::test_count();++i) {
            assert(std::strlen(tests[i].id)<sizeof(CertificationTestResult{}.test_id));
            assert(std::strlen(tests[i].id)<sizeof(CertificationStatusSummary{}.current_test));
            assert(std::strlen(tests[i].id)<sizeof(command.session_cert_test_id));
            assert(std::strlen(tests[i].id)<sizeof(session.certification_test_id));
        }
    } else if(name=="required_skipped" || name=="required_blocked" || name=="all_required_passed") {
        seed(service,"BOOT-001");
        auto& record=service.store_.current_record();
        record.result_count=std::min(CertificationPlan::test_count(),kMaxCertificationTests);
        record.session.test_count=record.result_count;
        const auto* tests=CertificationPlan::test_definitions();
        for(size_t i=0;i<record.result_count;++i) {
            std::strncpy(record.results[i].test_id,tests[i].id,sizeof(record.results[i].test_id)-1);
            record.results[i].state=CertificationTestState::Passed;
        }
        if(name!="all_required_passed")
            record.results[0].state=name=="required_skipped" ? CertificationTestState::Skipped : CertificationTestState::Blocked;
        service.evaluate_overall_state();
        assert(record.session.state==(name=="all_required_passed" ? CertificationState::Passed : CertificationState::Incomplete));
    } else if(name=="missing_required") {
        seed(service);
        service.store_.current_record().result_count=0;
        service.evaluate_overall_state();
        assert(service.session().state!=CertificationState::Passed);
    } else if(name=="instrument_evidence" || name=="nan_measurement" || name=="infinite_measurement" ||
              name=="wrong_units" || name=="missing_units" || name=="explicit_failure") {
        seed(service);
        const auto source=name=="instrument_evidence" ? EvidenceSource::Automatic : EvidenceSource::InstrumentMeasurement;
        const float value=name=="nan_measurement" ? std::numeric_limits<float>::quiet_NaN() :
            name=="infinite_measurement" ? std::numeric_limits<float>::infinity() : 1000.0f;
        const char* unit=name=="wrong_units" ? "V" : name=="missing_units" ? "" : "mW";
        const auto state=name=="explicit_failure" ? CertificationTestState::Failed : CertificationTestState::Passed;
        const int result=service.record_result("POWER-LIMIT-001",state,source,value,unit,"Instrument observation");
        if(name=="explicit_failure") {
            assert(result==ESP_OK);
            assert(service.get_test_result("POWER-LIMIT-001")->state==CertificationTestState::Failed);
        } else {
            assert(result==ESP_ERR_INVALID_ARG);
            assert(service.get_test_result("POWER-LIMIT-001")->state!=CertificationTestState::Passed);
        }
    } else if(name=="session_exclusivity") {
        seed(service);
        assert(service.start_session(CertificationProfile::Development,"test")==ESP_ERR_INVALID_STATE);
    } else if(name=="configuration_read_failure") {
        service.store_.init();
        ConfigStore config; service.config_=&config; config_error=ESP_FAIL;
        assert(service.start_session(CertificationProfile::Development,"test")==ESP_FAIL);
        assert(service.session().state==CertificationState::NotStarted);
    } else if(name=="save_failure_not_passed") {
        seed(service,"BOOT-001");
        save_error=ESP_ERR_NO_MEM;
        assert(service.record_result("BOOT-001",CertificationTestState::Passed,
            EvidenceSource::Automatic,0,"","observed")==ESP_ERR_NO_MEM);
        assert(service.session().state==CertificationState::Incomplete);
        assert(service.is_stale() && std::strcmp(service.stale_reason(),"persistence_failed")==0);
    } else if(name=="unimplemented_probes_blocked") {
        for (const char* id : {"BOOT-REBOOT-001","SAFE-ESTOP-LOGIC"}) {
            seed(service,id); ConfigStore config; service.config_=&config;
            assert(service.start_test(id)==ESP_OK);
            assert(service.get_test_result(id)->state==CertificationTestState::Blocked);
        }
    } else if(name=="start_full_session") {
        service.store_.init();
        assert(service.start_session(CertificationProfile::Development,"test")==ESP_OK);
        assert(service.record().result_count==CertificationPlan::test_count());
        for(size_t i=0;i<service.record().result_count;++i)
            assert(std::strcmp(service.record().results[i].test_id,CertificationPlan::test_definitions()[i].id)==0);
    } else if(name=="external_measurement_waits") {
        seed(service,"POWER-LIMIT-001");
        assert(service.start_test("POWER-LIMIT-001")==ESP_OK);
        assert(service.get_test_result("POWER-LIMIT-001")->state==CertificationTestState::WaitingForOperator);
        assert(service.start_test("BOOT-001")==ESP_ERR_INVALID_STATE);
    } else if(name=="long_uptime_timeout") {
        seed(service,"BOOT-OUTPUT-SAFE");
        constexpr uint64_t long_uptime_ms=(1ULL<<32)+1000;
        fake_clock_us=long_uptime_ms*1000;
        assert(service.start_test("BOOT-OUTPUT-SAFE")==ESP_OK);
        service.tick(long_uptime_ms+1000);
        assert(service.get_test_result("BOOT-OUTPUT-SAFE")->state==CertificationTestState::WaitingForOperator);
        service.tick(long_uptime_ms+300001);
        assert(service.get_test_result("BOOT-OUTPUT-SAFE")->state==CertificationTestState::Blocked);
    } else if(name=="abort_burn_in") {
        seed(service,"BURN-001");
        FactoryRunner runner; JobManager jobs;
        service.runner_=&runner; service.job_mgr_=&jobs;
        service.burn_in_active_=true; service.burn_in_job_id_=42;
        std::strcpy(service.current_test_,"BURN-001");
        assert(service.abort()==ESP_OK);
        assert(jobs.cancellations==1 && jobs.cancelled_job==42);
        assert(runner.aborts==1 && !service.burn_in_active_);
        assert(service.current_running_test()[0]=='\0');
        assert(service.get_test_result("BURN-001")->state==CertificationTestState::Blocked);
        assert(service.start_session(CertificationProfile::Development,"next")==ESP_OK);
    } else if(name=="report_image_identity") {
        seed(service,"BOOT-001");
        cJSON* report=service.generate_report_json(); assert(report);
        const auto* hash=cJSON_GetObjectItemCaseSensitive(report,"reportingFirmwareElfSha256");
        assert(cJSON_IsString(hash) && std::strlen(hash->valuestring)==64);
        assert(std::strncmp(hash->valuestring,"a5",2)==0);
        const auto* version=cJSON_GetObjectItemCaseSensitive(report,"reportingFirmwareVersion");
        assert(cJSON_IsString(version) && std::strcmp(version->valuestring,"reporting-image")==0);
        cJSON_Delete(report);
    } else { assert(false); }
    std::cout << "PASS " << name << '\n';
}
'''


def main():
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not compiler or not cc:
        raise SystemExit("Host C and C++ compilers are required")
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    with tempfile.TemporaryDirectory(prefix="lumigrid-certification-service-") as directory:
        build = Path(directory)
        for name, content in STUBS.items():
            (build / name).write_text(content)
        (build / "harness.cpp").write_text(HARNESS)
        subprocess.run([cc,"-c",str(cjson/"cJSON.c"),"-I",str(cjson),"-o",str(build/"cjson.o")],check=True)
        executable=build/"certification_service"
        subprocess.run([compiler,"-std=c++17","-O1","-g","-Wall","-Wextra",
                        "-I",str(build),"-I",str(ROOT/"components/certification/include"),
                        "-I",str(ROOT/"components/factory/include"),"-I",str(cjson),
                        str(ROOT/"components/certification/CertificationPlan.cpp"),
                        str(ROOT/"components/certification/CertificationService.cpp"),
                        str(build/"harness.cpp"),str(build/"cjson.o"),"-o",str(executable)],check=True)
        cases=("plan_bounds","required_skipped","required_blocked","all_required_passed",
               "missing_required","instrument_evidence","nan_measurement","infinite_measurement",
               "wrong_units","missing_units","explicit_failure","session_exclusivity",
               "configuration_read_failure","save_failure_not_passed","unimplemented_probes_blocked",
               "start_full_session","external_measurement_waits","long_uptime_timeout","abort_burn_in","report_image_identity")
        failures=[]
        for case in cases:
            result=subprocess.run([str(executable),case],capture_output=True,text=True)
            print(result.stdout or result.stderr,end="")
            if result.returncode: failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(cases)} certification cases failed: {', '.join(failures)}")
        print(f"PASS: {len(cases)} production certification scenarios")


if __name__=="__main__":
    main()
