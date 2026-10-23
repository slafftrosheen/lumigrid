"""Exercise production OTA boot state transitions with fault-injected IDF/NVS.

These host cases verify error handling and state truth. They do not emulate the
bootloader's electrical power-loss behavior or replace board rollback tests.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = {
    "esp_err.h": r'''
#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101,
 ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
 ESP_ERR_INVALID_SIZE=0x104, ESP_ERR_NOT_FOUND=0x105,
 ESP_ERR_NOT_SUPPORTED=0x106, ESP_ERR_INVALID_CRC=0x109,
 ESP_ERR_NVS_NOT_FOUND=0x1102, ESP_ERR_NVS_INVALID_LENGTH=0x110c,
 ESP_ERR_OTA_ROLLBACK_FAILED=0x1506;
inline const char* esp_err_to_name(int) { return "injected_error"; }
''',
    "esp_app_desc.h": r'''
#pragma once
#include <cstdint>
constexpr uint32_t ESP_APP_DESC_MAGIC_WORD=0xabcd5432;
struct esp_app_desc_t {
 uint32_t magic_word=ESP_APP_DESC_MAGIC_WORD, secure_version=0, reserv1[2]{};
 char version[32]="1.0.0-test", project_name[32]="lumigrid_led_node";
 char time[16]{},date[16]{},idf_ver[32]{}; uint8_t app_elf_sha256[32]{};
 uint32_t reserv2[20]{};
};
inline esp_app_desc_t app_description;
inline const esp_app_desc_t* esp_app_get_description() { return &app_description; }
''',
    "AuthTypes.hpp": r'''
#pragma once
namespace lg::auth { enum class CommissioningState { None, FactoryFirstBoot, PhysicalRecovery }; }
''',
    "freertos/FreeRTOS.h": r'''
#pragma once
#include <cstdint>
using TickType_t=uint32_t;
constexpr int pdTRUE=1, portMAX_DELAY=-1;
''',
    "freertos/semphr.h": r'''
#pragma once
#include <mutex>
using SemaphoreHandle_t=std::recursive_mutex*;
inline bool fail_mutex_allocation=false;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { static std::recursive_mutex mutex; return fail_mutex_allocation ? nullptr : &mutex; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t mutex,int) { mutex->lock(); return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t mutex) { mutex->unlock(); return 1; }
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
inline uint64_t fake_now_ms=1000;
inline int64_t esp_timer_get_time() { return fake_now_ms*1000; }
''',
    "esp_system.h": r'''
#pragma once
enum esp_reset_reason_t { ESP_RST_POWERON=1, ESP_RST_SW=3, ESP_RST_INT_WDT=5, ESP_RST_TASK_WDT=6, ESP_RST_WDT=7 };
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
''',
    "esp_rom_crc.h": r'''
#pragma once
#include <cstddef>
#include <cstdint>
inline uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t* bytes,size_t count) {
 crc=~crc;
 for(size_t i=0;i<count;++i) { crc^=bytes[i]; for(int b=0;b<8;++b) crc=(crc>>1)^(0xedb88320U & -(crc&1)); }
 return ~crc;
}
''',
    "nvs_flash.h": "#pragma once\n",
    "nvs.h": r'''
#pragma once
#include "esp_err.h"
#include <algorithm>
#include <cstring>
#include <vector>
using nvs_handle_t=int;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
inline int open_error=ESP_OK,get_error=ESP_OK,set_error=ESP_OK,commit_error=ESP_OK;
inline int set_calls=0,commit_calls=0;
inline std::vector<uint8_t> persisted, staged;
inline int nvs_open(const char*,int,nvs_handle_t* out) { *out=1; return open_error; }
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_blob(nvs_handle_t,const char*,void* output,size_t* bytes) {
 if(get_error!=ESP_OK) return get_error;
 if(persisted.empty()) return ESP_ERR_NVS_NOT_FOUND;
 if(!output) { *bytes=persisted.size(); return ESP_OK; }
 if(*bytes<persisted.size()) { *bytes=persisted.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
 *bytes=persisted.size(); std::memcpy(output,persisted.data(),*bytes); return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t,const char*,const void* data,size_t bytes) {
 ++set_calls; if(set_error!=ESP_OK) return set_error;
 const auto* p=static_cast<const uint8_t*>(data); staged.assign(p,p+bytes); return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) {
 ++commit_calls; if(commit_error!=ESP_OK) return commit_error;
 persisted=staged; return ESP_OK;
}
''',
    "esp_ota_ops.h": r'''
#pragma once
#include "esp_err.h"
#include <cstdint>
#include <cstring>
#include <initializer_list>
enum esp_partition_type_t { ESP_PARTITION_TYPE_APP=0 };
enum esp_partition_subtype_t { ESP_PARTITION_SUBTYPE_APP_FACTORY=0,
 ESP_PARTITION_SUBTYPE_APP_OTA_MIN=0x10,ESP_PARTITION_SUBTYPE_APP_OTA_0=0x10,
 ESP_PARTITION_SUBTYPE_APP_OTA_1=0x11,ESP_PARTITION_SUBTYPE_ANY=0xff };
struct esp_partition_t { esp_partition_type_t type; esp_partition_subtype_t subtype;
 uint32_t address,size; char label[17]; };
inline esp_partition_t factory{ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,0x250000,"factory"};
inline esp_partition_t ota0{ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,0x260000,0x250000,"ota_0"};
inline esp_partition_t ota1{ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,0x4b0000,0x250000,"ota_1"};
inline const esp_partition_t* running_partition=&factory;
enum esp_ota_img_states_t { ESP_OTA_IMG_NEW,ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID,
 ESP_OTA_IMG_INVALID,ESP_OTA_IMG_ABORTED,ESP_OTA_IMG_UNDEFINED };
inline esp_ota_img_states_t ota0_state=ESP_OTA_IMG_VALID,ota1_state=ESP_OTA_IMG_VALID;
inline int state_error=ESP_OK,confirm_error=ESP_OK,rollback_error=ESP_ERR_OTA_ROLLBACK_FAILED;
inline int confirm_calls=0,rollback_calls=0;
inline bool rollback_reboots=false;
struct RebootRequested {};
inline const esp_partition_t* esp_ota_get_running_partition() { return running_partition; }
inline const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char* label) {
 for(const auto* p:{&factory,&ota0,&ota1}) if(label && std::strcmp(label,p->label)==0) return p;
 return nullptr;
}
inline int esp_ota_get_state_partition(const esp_partition_t* p,esp_ota_img_states_t* out) {
 if(state_error!=ESP_OK) return state_error;
 if(p==&factory) return ESP_ERR_NOT_SUPPORTED;
 if(p==&ota0) { *out=ota0_state; return ESP_OK; }
 if(p==&ota1) { *out=ota1_state; return ESP_OK; }
 return ESP_ERR_NOT_FOUND;
}
inline int esp_ota_mark_app_valid_cancel_rollback() {
 ++confirm_calls; if(confirm_error==ESP_OK) (running_partition==&ota0 ? ota0_state : ota1_state)=ESP_OTA_IMG_VALID;
 return confirm_error;
}
inline int esp_ota_mark_app_invalid_rollback_and_reboot() {
 ++rollback_calls;
 if(rollback_reboots) { (running_partition==&ota0 ? ota0_state : ota1_state)=ESP_OTA_IMG_INVALID; throw RebootRequested{}; }
 return rollback_error;
}
''',
}

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include "OtaBootVerifier.hpp"
#include "nvs.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
using namespace lg;
void store(PersistedOtaState value) {
 value.checksum=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(&value),sizeof(value)-4);
 auto* p=reinterpret_cast<uint8_t*>(&value); persisted.assign(p,p+sizeof(value));
}
PersistedOtaState intent(OtaBootState state=OtaBootState::PendingVerify,const char* target="ota_0") {
 PersistedOtaState st{}; st.state=state;
 std::strcpy(st.previous_version,"1.0.0-old"); std::strcpy(st.target_version,app_description.version);
 std::strcpy(st.target_partition,target); return st;
}
void candidate(OtaBootVerifier& v) {
 running_partition=&ota0; ota0_state=ESP_OTA_IMG_PENDING_VERIFY; store(intent());
 assert(v.init()==ESP_OK && v.is_pending_verify());
}
// Preserve a behavioral baseline for older source trees which only have the
// normal rollback entry point. The new startup path must work before init.
template<class T> auto failed_startup(T& v,int) -> decltype(v.rollback_failed_startup("startup_failure",-42)) {
 return v.rollback_failed_startup("startup_failure",-42);
}
template<class T> esp_err_t failed_startup(T& v,long) {
 return v.rollback_candidate("startup_failure",-42);
}
int main(int argc,char** argv) {
 assert(argc==2); std::string name=argv[1]; auto& v=OtaBootVerifier::instance();
 if(name=="same_version_rollback") {
  running_partition=&ota0; ota1_state=ESP_OTA_IMG_ABORTED;
  store(intent(OtaBootState::PendingVerify,"ota_1"));
  assert(v.init()==ESP_OK); assert(v.boot_state()==OtaBootState::RolledBack);
 } else if(name=="wrong_partition_not_confirmed") {
  store(intent(OtaBootState::PendingReboot,"ota_1"));
  assert(v.init()==ESP_OK); assert(v.boot_state()==OtaBootState::Failed);
 } else if(name=="unknown_image_state") {
  running_partition=&ota0; store(intent()); state_error=ESP_FAIL;
  assert(v.init()!=ESP_OK); assert(v.boot_state()!=OtaBootState::Confirmed);
 } else if(name=="journal_read_error") {
  open_error=ESP_FAIL; assert(v.init()==ESP_FAIL);
 } else if(name=="pending_despite_journal_error") {
  running_partition=&ota0; ota0_state=ESP_OTA_IMG_PENDING_VERIFY; open_error=ESP_FAIL;
  assert(v.init()==ESP_FAIL); assert(v.is_pending_verify());
 } else if(name=="unterminated_journal" || name=="invalid_journal_enum") {
  auto st=intent();
  if(name=="unterminated_journal") std::memset(st.target_version,'x',sizeof(st.target_version));
  else st.state=static_cast<OtaBootState>(255);
  store(st); assert(v.init()!=ESP_OK); assert(v.boot_state()!=OtaBootState::Confirmed);
 } else if(name=="failed_save_not_published") {
  assert(v.init()==ESP_OK); auto before=v.state(); auto next=intent(OtaBootState::PendingReboot);
  commit_error=ESP_FAIL; assert(v.save_state(next)==ESP_FAIL);
  assert(v.boot_state()==before.state && std::strcmp(v.state().target_partition,before.target_partition)==0);
 } else if(name=="confirm_non_candidate") {
  assert(v.init()==ESP_OK); auto before=v.state();
  assert(v.confirm_candidate()==ESP_ERR_INVALID_STATE); assert(v.boot_state()==before.state && confirm_calls==0);
 } else if(name=="rollback_non_candidate") {
  assert(v.init()==ESP_OK); auto before=v.state();
  assert(v.rollback_candidate("not_a_candidate")==ESP_ERR_INVALID_STATE);
  assert(v.boot_state()==before.state && rollback_calls==0);
 } else if(name=="healthy_dwell" || name=="confirmation_error" || name=="dwell_reset") {
  candidate(v); assert(v.confirm_candidate()==ESP_ERR_INVALID_STATE);
  assert(v.observe_runtime(true,false,false,1000)==ESP_OK && confirm_calls==0);
  assert(v.observe_runtime(true,false,false,10999)==ESP_OK && confirm_calls==0);
  if(name=="dwell_reset") {
   assert(v.observe_runtime(false,false,false,11000)==ESP_OK);
   assert(v.observe_runtime(true,false,false,12000)==ESP_OK && confirm_calls==0);
   assert(v.observe_runtime(true,false,false,21999)==ESP_OK && confirm_calls==0);
   assert(v.observe_runtime(true,false,false,22000)==ESP_OK);
  } else {
   if(name=="confirmation_error") confirm_error=ESP_FAIL;
   assert(v.observe_runtime(true,false,false,11000)==confirm_error);
  }
  assert(confirm_calls==1);
  assert(v.is_pending_verify()==(name=="confirmation_error"));
  assert(v.boot_state()==(name=="confirmation_error" ? OtaBootState::PendingVerify : OtaBootState::Confirmed));
 } else if(name=="dwell_save_error") {
  candidate(v); commit_error=ESP_FAIL;
  assert(v.observe_runtime(true,false,false,1000)==ESP_FAIL);
  v.observe_runtime(true,false,false,11000);
  assert(confirm_calls==0 && v.is_pending_verify());
 } else if(name=="fatal_with_journal_error") {
  candidate(v); commit_error=ESP_FAIL;
  v.observe_runtime(true,true,false,1000);
  assert(rollback_calls==1 && confirm_calls==0 && v.boot_state()==OtaBootState::Failed);
 } else if(name=="rollback_failure_latched") {
  candidate(v); assert(v.observe_runtime(true,false,false,1000)==ESP_OK);
  assert(v.rollback_candidate("startup_failure",-42)==ESP_ERR_OTA_ROLLBACK_FAILED);
  assert(v.boot_state()==OtaBootState::Failed && v.state().last_error==ESP_ERR_OTA_ROLLBACK_FAILED);
  v.observe_runtime(true,false,false,12000);
  assert(confirm_calls==0 && v.is_pending_verify());
 } else if(name=="rollback_confirmed_after_reboot") {
  candidate(v); rollback_reboots=true;
  try { v.rollback_candidate("startup_failure",-42); assert(false); } catch(const RebootRequested&) {}
  assert(v.boot_state()!=OtaBootState::RolledBack);
  running_partition=&factory; assert(v.init()==ESP_OK);
  assert(v.boot_state()==OtaBootState::RolledBack && !v.is_pending_verify());
 } else if(name=="reinit_clears_candidate") {
  candidate(v); running_partition=&factory; persisted.clear();
  assert(v.init()==ESP_OK && !v.is_pending_verify());
 } else if(name=="startup_before_init" || name=="startup_before_init_no_nvs" ||
           name=="startup_before_init_no_mutex" || name=="startup_after_journal_failure") {
  running_partition=&ota0; ota0_state=ESP_OTA_IMG_PENDING_VERIFY;
  if(name=="startup_before_init_no_nvs" || name=="startup_after_journal_failure") open_error=ESP_FAIL;
  if(name=="startup_before_init_no_mutex") fail_mutex_allocation=true;
  if(name=="startup_after_journal_failure") assert(v.init()==ESP_FAIL && v.is_pending_verify());
  assert(failed_startup(v,0)==ESP_ERR_OTA_ROLLBACK_FAILED);
  assert(rollback_calls==1 && confirm_calls==0 && v.is_pending_verify());
  assert(v.boot_state()==OtaBootState::Failed && v.state().last_error==ESP_ERR_OTA_ROLLBACK_FAILED);
  assert(failed_startup(v,0)==ESP_ERR_OTA_ROLLBACK_FAILED && rollback_calls==1);
 } else if(name=="startup_non_candidate") {
  assert(failed_startup(v,0)==ESP_ERR_INVALID_STATE && rollback_calls==0);
  assert(v.boot_state()!=OtaBootState::RolledBack);
 } else if(name=="startup_query_error") {
  state_error=ESP_FAIL;
  assert(failed_startup(v,0)==ESP_FAIL && rollback_calls==0);
  assert(v.boot_state()!=OtaBootState::RolledBack);
 } else { assert(false); }
 std::cout << "PASS " << name << '\n';
}
'''

CASES = (
    "same_version_rollback", "wrong_partition_not_confirmed", "unknown_image_state",
    "journal_read_error", "pending_despite_journal_error", "unterminated_journal",
    "invalid_journal_enum", "failed_save_not_published", "confirm_non_candidate",
    "rollback_non_candidate", "healthy_dwell", "confirmation_error", "dwell_reset",
    "dwell_save_error", "fatal_with_journal_error", "rollback_failure_latched",
    "rollback_confirmed_after_reboot", "reinit_clears_candidate",
    "startup_before_init", "startup_before_init_no_nvs", "startup_before_init_no_mutex",
    "startup_after_journal_failure", "startup_non_candidate", "startup_query_error",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=ROOT)
    args = parser.parse_args()
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-ota-verify-") as directory:
        build = Path(directory)
        for path, content in STUBS.items():
            target = build / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        (build / "harness.cpp").write_text(HARNESS)
        root = args.source_root
        executable = build / "ota_verifier"
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
                        "-I", str(build), "-I", str(root / "components/ota/include"),
                        "-I", str(root / "components/config/include"),
                        str(root / "components/ota/OtaBootVerifier.cpp"),
                        str(build / "harness.cpp"), "-o", str(executable)], check=True)
        failures = []
        environment = dict(os.environ)
        # LeakSanitizer cannot enumerate processes in the restricted runner.
        # Address and undefined-behavior instrumentation remain enabled.
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        for case in CASES:
            result = subprocess.run([str(executable), case], capture_output=True, text=True, cwd=build, env=environment)
            print(result.stdout or result.stderr, end="")
            if result.returncode:
                failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(CASES)} OTA verifier scenarios failed: {', '.join(failures)}")
        print(f"PASS: {len(CASES)} production OTA verifier scenarios (ASan/UBSan)")


if __name__ == "__main__":
    main()
