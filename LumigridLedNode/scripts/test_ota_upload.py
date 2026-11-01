"""Run production OTA upload/status handlers with a sequential-flash test double.

The double preserves unwritten sectors and models esp_ota_end's full-partition
extent. The real preflight, handler and boot verifier must reject an upload
that would otherwise be completed by an old image tail. IDF signature/hash
verification and physical power interruption remain device acceptance gates.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

from test_ota_boot_verifier import STUBS as VERIFIER_STUBS

ROOT = Path(__file__).resolve().parents[1]
STUBS = dict(VERIFIER_STUBS)
STUBS["nvs.h"] = STUBS["nvs.h"].replace(
    "inline int set_calls=0,commit_calls=0;",
    "inline int set_calls=0,commit_calls=0; inline void (*commit_hook)()=nullptr;",
).replace("persisted=staged; return ESP_OK;", "persisted=staged; if(commit_hook) commit_hook(); return ESP_OK;")
STUBS["esp_app_format.h"] = r'''
#pragma once
#include <cstdint>
constexpr uint16_t ESP_CHIP_ID_ESP32=0;
constexpr uint8_t ESP_IMAGE_HEADER_MAGIC=0xe9,ESP_IMAGE_FLASH_SIZE_8MB=3;
struct __attribute__((packed)) esp_image_header_t {
 uint8_t magic=ESP_IMAGE_HEADER_MAGIC,segment_count=1,spi_mode=0;
 uint8_t spi_speed:4,spi_size:4;
 uint32_t entry_addr=0; uint8_t wp_pin=0,spi_pin_drv[3]{};
 uint16_t chip_id=ESP_CHIP_ID_ESP32; uint8_t min_chip_rev=0;
 uint16_t min_chip_rev_full=0,max_chip_rev_full=0xffff;
 uint8_t reserved[4]{},hash_appended=0;
};
struct esp_image_segment_header_t { uint32_t load_addr=0x3f400020,data_len=0; };
static_assert(sizeof(esp_image_header_t)==24);
static_assert(sizeof(esp_app_desc_t)==256);
'''
STUBS["esp_image_format.h"] = r'''
#pragma once
#include "esp_err.h"
#include <cstdint>
struct esp_partition_pos_t { uint32_t offset,size; };
struct esp_image_metadata_t { uint32_t image_len=0; };
constexpr int ESP_IMAGE_VERIFY=0,ESP_IMAGE_VERIFY_SILENT=1;
int esp_image_verify(int,const esp_partition_pos_t*,esp_image_metadata_t*);
'''
STUBS["esp_ota_ops.h"] += r'''
#include <cassert>
#include <algorithm>
#include <vector>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
using esp_ota_handle_t=uint32_t;
constexpr size_t OTA_WITH_SEQUENTIAL_WRITES=static_cast<size_t>(-2);
constexpr int ESP_ERR_OTA_VALIDATE_FAILED=0x1503,ESP_ERR_IMAGE_INVALID=0x2002;
inline const esp_partition_t* selected_boot=&factory;
inline const esp_partition_t* update_partition=&ota1;
inline int begin_calls=0,write_calls=0,abort_calls=0,end_calls=0,select_calls=0,bounded_verify_calls=0;
inline int begin_error=ESP_OK,write_error_call=0,end_error=ESP_OK,select_error=ESP_OK;
inline bool active_handle=false,fail_reselect=false;
inline bool invalid_final_descriptor=false;
inline void (*selection_hook)()=nullptr;
inline std::vector<uint8_t> image,flash(0x250000,0xff);
inline size_t written=0;
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return update_partition; }
inline int verify_flash(size_t extent,esp_image_metadata_t* metadata) {
 if(flash.size()<32) return ESP_ERR_IMAGE_INVALID;
 esp_image_header_t header{}; esp_image_segment_header_t segment{};
 std::memcpy(&header,flash.data(),sizeof(header));
 std::memcpy(&segment,flash.data()+sizeof(header),sizeof(segment));
 // This fixture has one segment. Its checksum is padded to a 16-byte boundary;
 // the optional 32-byte appended digest is part of the complete image extent.
 uint64_t length=(sizeof(header)+sizeof(segment)+static_cast<uint64_t>(segment.data_len)+16)&~15ULL;
 if(header.hash_appended) length+=32;
 if(length>extent || length>flash.size() || length!=image.size() ||
    !std::equal(image.begin(),image.end(),flash.begin())) return ESP_ERR_IMAGE_INVALID;
 if(metadata) metadata->image_len=length;
 return ESP_OK;
}
inline int esp_image_verify(int,const esp_partition_pos_t* pos,esp_image_metadata_t* metadata) {
 ++bounded_verify_calls; return verify_flash(pos->size,metadata);
}
inline int esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t* handle) {
 ++begin_calls; if(begin_error!=ESP_OK) return begin_error;
 *handle=1; active_handle=true; written=0; return ESP_OK;
}
inline int esp_ota_write(esp_ota_handle_t,const void* data,size_t size) {
 assert(active_handle); ++write_calls;
 if(write_error_call==write_calls) return ESP_FAIL;
 size_t first=written/4096,last=(written+size-1)/4096;
 if(written%4096) ++first;
 if(first<=last) std::fill(flash.begin()+first*4096,flash.begin()+(last+1)*4096,0xff);
 std::memcpy(flash.data()+written,data,size); written+=size; return ESP_OK;
}
inline int esp_ota_abort(esp_ota_handle_t) { assert(active_handle); ++abort_calls; active_handle=false; return ESP_OK; }
inline int esp_ota_end(esp_ota_handle_t) {
 assert(active_handle); ++end_calls; active_handle=false;
 if(end_error!=ESP_OK) return end_error;
 return verify_flash(update_partition->size,nullptr);
}
inline int esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t* desc) {
 std::memcpy(desc,flash.data()+sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t),sizeof(*desc));
 if(invalid_final_descriptor) std::memset(desc->version,'x',sizeof(desc->version));
 return ESP_OK;
}
inline int esp_ota_set_boot_partition(const esp_partition_t* partition) {
 ++select_calls;
 if(select_error!=ESP_OK || (fail_reselect && partition==&factory)) return ESP_FAIL;
 selected_boot=partition; if(selection_hook) selection_hook(); return ESP_OK;
}
'''

HARNESS = r'''
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <inttypes.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "OtaBootVerifier.hpp"
#include "OtaImagePreflight.hpp"
#include "esp_image_format.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
struct SimulatedPowerCut {};
constexpr int HTTPD_SOCK_ERR_TIMEOUT=-3;
struct httpd_req_t { void* user_ctx=nullptr; size_t content_len=0; std::vector<uint8_t> input;
 size_t offset=0; std::string status,body,expected_node; };
size_t httpd_req_get_hdr_value_len(httpd_req_t* req,const char*) { return req->expected_node.size(); }
int httpd_req_get_hdr_value_str(httpd_req_t* req,const char*,char* dst,size_t size) {
 if(req->expected_node.size()>=size) return ESP_FAIL;
 std::memcpy(dst,req->expected_node.c_str(),req->expected_node.size()+1); return ESP_OK;
}
inline int receive_calls=0,restart_calls=0;
inline size_t receive_chunk=512;
inline bool intermittent_prefix_timeout=false,body_timeout=false;
int httpd_req_recv(httpd_req_t* request,char* output,size_t maximum) {
 ++receive_calls;
 if(intermittent_prefix_timeout && receive_calls<7 && receive_calls%2==1) return HTTPD_SOCK_ERR_TIMEOUT;
 if(body_timeout && request->offset>=lg::kOtaDescriptorPrefixBytes) return HTTPD_SOCK_ERR_TIMEOUT;
 size_t count=std::min({maximum,receive_chunk,request->input.size()-request->offset});
 if(count) std::memcpy(output,request->input.data()+request->offset,count);
 request->offset+=count; return count;
}
int httpd_resp_set_status(httpd_req_t* req,const char* status) { req->status=status; return ESP_OK; }
int httpd_resp_set_type(httpd_req_t*,const char*) { return ESP_OK; }
int httpd_resp_sendstr(httpd_req_t* req,const char* text) { req->body=text; return ESP_OK; }
struct esp_chip_info_t { uint16_t revision=301; };
void esp_chip_info(esp_chip_info_t* chip) { *chip={}; }
void vTaskDelay(int) {}
#define pdMS_TO_TICKS(x) (x)
void esp_restart() { ++restart_calls; }
#include "esp_log.h"
namespace lg {
namespace api {
enum class ApiRole { Admin,Operator };
bool authorize(httpd_req_t*,ApiRole) { return true; }
class AuditLog { public: static AuditLog& instance() { static AuditLog log; return log; }
 void log(ApiRole,const char*,const char*) {} };
}
enum class CommandDomain { Ota };
enum class JobType { Ota };
enum class JobState { Preparing,Running,Verifying };
struct FakeJournal { int accepted=0,failures=0,applied_count=0;
 uint32_t accept(CommandDomain) { return ++accepted; }
 void failed(uint32_t,int) { ++failures; }
 void applied(uint32_t) { ++applied_count; }
};
struct FakeJobs { int failures=0; int start_job(JobType,uint32_t* id) { *id=1; return ESP_OK; }
 void update_progress(uint32_t,JobState,uint8_t) {}
 void fail_job(uint32_t,int,const char*) { ++failures; }
};
class FactoryApi { public: FakeJournal journal; FakeJobs jobs;
 struct Identity { char id[24]="lg-test-node"; };
 Identity identity() const { return {}; }
 MaintenanceMode current_maintenance_mode() { return MaintenanceMode::None; }
 FakeJournal& command_journal() { return journal; }
 FakeJobs& job_manager() { return jobs; }
 void notify_status_async() {}
};
struct MemoryAdmissionResult { bool accepted=true; uint32_t required_bytes=2048,reserve_bytes=8192; };
MemoryAdmissionResult check_memory_admission(uint32_t) { return {}; }
struct RuntimeMemory { uint32_t free_8bit=100000; };
RuntimeMemory capture_runtime_memory() { return {}; }
enum class StorageHealth { Healthy };
const char* storage_health_name(StorageHealth) { return "HEALTHY"; }
struct StorageSnapshot { StorageHealth health=StorageHealth::Healthy;
 const esp_partition_t* running=running_partition; const esp_partition_t* next=update_partition; uint32_t image_bytes=0; };
StorageSnapshot capture_storage() { return {}; }
constexpr uint16_t kPartitionLayoutVersion=2;
int send_error(httpd_req_t* req,const char* status,const char* message) { req->status=status; req->body=message; return ESP_OK; }
int send_json(httpd_req_t* req,const char* text,const char* status="200 OK") { req->status=status; req->body=text ? text : ""; return ESP_OK; }
int send_json(httpd_req_t* req,cJSON* value,const char* status="200 OK") {
 char* text=cJSON_PrintUnformatted(value); cJSON_Delete(value);
 int result=send_json(req,text,status); cJSON_free(text); return result;
}
void note_api_allocation_failure() {}
__HANDLERS__
}
void make_image(bool hash=false,const char* version="2.0.0-test") {
 esp_image_header_t header{}; header.spi_size=ESP_IMAGE_FLASH_SIZE_8MB; header.hash_appended=hash;
 esp_image_segment_header_t segment{}; segment.data_len=8144;
 esp_app_desc_t description{}; std::strncpy(description.version,version,sizeof(description.version)-1);
 image.assign(hash ? 8224 : 8192,0x42);
 std::memcpy(image.data(),&header,sizeof(header));
 std::memcpy(image.data()+sizeof(header),&segment,sizeof(segment));
 std::memcpy(image.data()+sizeof(header)+sizeof(segment),&description,sizeof(description));
}
int main(int argc,char** argv) {
 using namespace lg; assert(argc==2); std::string name=argv[1];
 auto& verifier=OtaBootVerifier::instance();
 if(name=="pending_candidate_rejected") { running_partition=&ota0; ota0_state=ESP_OTA_IMG_PENDING_VERIFY; }
 assert(verifier.init()==ESP_OK);
 FactoryApi api; httpd_req_t request; request.user_ctx=&api;
 make_image(name=="truncated_hash_tail" || name=="valid_hash_image",name=="version_json_escaped" ? "rc\"quoted" : "2.0.0-test");
 if(name=="invalid_project") image[sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+offsetof(esp_app_desc_t,project_name)]='x';
 else if(name=="invalid_magic") image[0]=0;
 else if(name=="invalid_chip") image[offsetof(esp_image_header_t,chip_id)]=5;
 else if(name=="unterminated_version") std::memset(image.data()+sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+offsetof(esp_app_desc_t,version),'x',32);
 request.input=image; request.content_len=image.size();
 if(name=="target_identity_match") request.expected_node="lg-test-node";
 else if(name=="target_identity_mismatch") request.expected_node="lg-different-node";
 else if(name=="target_identity_oversized") request.expected_node=std::string(32,'x');
 if(name=="truncated_old_tail" || name=="truncated_hash_tail") {
  std::copy(image.begin(),image.end(),flash.begin());
  request.input.resize(name=="truncated_old_tail" ? 4096 : 8192); request.content_len=request.input.size();
 } else if(name=="short_transport") request.input.resize(4096);
 else if(name=="oversized") request.content_len=update_partition->size+1;
 else if(name=="prefix_write_error") write_error_call=1;
 else if(name=="body_write_error") write_error_call=3;
 else if(name=="body_timeouts") body_timeout=true;
 else if(name=="end_validation_error") end_error=ESP_ERR_OTA_VALIDATE_FAILED;
 else if(name=="invalid_final_descriptor") invalid_final_descriptor=true;
 else if(name=="boot_selection_error") select_error=ESP_FAIL;
 else if(name=="boot_selection_and_journal_error") {
  select_error=ESP_FAIL; commit_hook=[] { commit_error=ESP_FAIL; };
 }
 else if(name=="journal_error_no_boot_change") { commit_error=ESP_FAIL; fail_reselect=true; }
 else if(name=="prefix_timeout_progress") { intermittent_prefix_timeout=true; receive_chunk=32; }
 else if(name=="power_cut_before_selection") commit_hook=[] { throw SimulatedPowerCut{}; };
 else if(name=="power_cut_after_selection") selection_hook=[] { throw SimulatedPowerCut{}; };
 else if(name=="status_failure_truth" || name=="status_persistence_error") {
  ota0_state=ESP_OTA_IMG_PENDING_VERIFY; running_partition=&ota0; assert(verifier.init()==ESP_OK);
  if(name=="status_persistence_error") commit_error=ESP_FAIL;
  verifier.rollback_candidate("test_failure");
  request={}; request.user_ctx=&api; assert(get_ota_status(&request)==ESP_OK);
  cJSON* result=cJSON_Parse(request.body.c_str()); assert(result);
  assert(std::strcmp(cJSON_GetObjectItem(result,"nodeId")->valuestring,"lg-test-node")==0);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(result,"targetBinding")));
  assert(cJSON_IsTrue(cJSON_GetObjectItem(result,"pendingVerification")));
  assert(cJSON_GetObjectItem(result,"lastError")->valueint==ESP_ERR_OTA_ROLLBACK_FAILED);
  assert(cJSON_GetObjectItem(result,"persistenceError")->valueint==commit_error);
  assert(!cJSON_IsTrue(cJSON_GetObjectItem(result,"rolledBack"))); cJSON_Delete(result);
  std::cout << "PASS " << name << '\n'; return 0;
 }
 bool cut=false;
 try { assert(post_ota(&request)==ESP_OK); } catch(const SimulatedPowerCut&) { cut=true; }
 assert(!active_handle);
 if(name=="power_cut_before_selection" || name=="power_cut_after_selection") {
  assert(cut && persisted.size()==sizeof(PersistedOtaState));
  PersistedOtaState saved{}; std::memcpy(&saved,persisted.data(),sizeof(saved));
  assert(saved.state==OtaBootState::PendingReboot && std::strcmp(saved.target_partition,"ota_1")==0);
  if(name=="power_cut_before_selection") {
   assert(select_calls==0 && selected_boot==&factory);
   commit_hook=nullptr; assert(verifier.init()==ESP_OK); assert(verifier.boot_state()==OtaBootState::Failed);
  } else assert(selected_boot==update_partition && select_calls==1);
 } else if(name=="valid_image" || name=="valid_hash_image" || name=="prefix_timeout_progress" || name=="version_json_escaped" || name=="target_identity_match") {
  assert(request.status=="200 OK" && restart_calls==1 && selected_boot==update_partition);
  cJSON* result=cJSON_Parse(request.body.c_str()); assert(result && cJSON_IsTrue(cJSON_GetObjectItem(result,"accepted")));
  assert(std::strcmp(cJSON_GetObjectItem(result,"nodeId")->valuestring,"lg-test-node")==0);
  assert(std::strcmp(cJSON_GetObjectItem(result,"targetPartition")->valuestring,"ota_1")==0);
  if(name=="version_json_escaped") assert(std::strcmp(cJSON_GetObjectItem(result,"targetVersion")->valuestring,"rc\"quoted")==0);
  cJSON_Delete(result);
  assert(verifier.boot_state()==OtaBootState::PendingReboot);
 } else {
  assert(request.status!="200 OK" && restart_calls==0 && selected_boot==&factory);
  if(name=="journal_error_no_boot_change") assert(select_calls==0 && verifier.boot_state()!=OtaBootState::PendingReboot);
  else if(name=="boot_selection_error" || name=="boot_selection_and_journal_error") {
   assert(verifier.boot_state()==OtaBootState::Failed && verifier.state().last_error==ESP_FAIL);
   assert(select_calls==1);
   httpd_req_t status; status.user_ctx=&api; assert(get_ota_status(&status)==ESP_OK);
   cJSON* result=cJSON_Parse(status.body.c_str()); assert(result);
   const cJSON* error=cJSON_GetObjectItem(result,"persistenceError");
   assert(error && error->valueint==commit_error); cJSON_Delete(result);
  }
  else assert(select_calls==0);
  if(name=="pending_candidate_rejected" || name=="invalid_magic" || name=="invalid_chip" ||
     name=="invalid_project" || name=="unterminated_version" || name=="oversized" ||
     name=="target_identity_mismatch" || name=="target_identity_oversized") assert(begin_calls==0);
  if(name=="target_identity_mismatch" || name=="target_identity_oversized") assert(receive_calls==0 && write_calls==0);
  if(name=="short_transport" || name=="prefix_write_error" || name=="body_write_error" || name=="body_timeouts") assert(abort_calls==1);
 }
 std::cout << "PASS " << name << '\n';
}
'''

CASES = (
    "target_identity_match", "target_identity_mismatch", "target_identity_oversized",
    "valid_image", "valid_hash_image", "truncated_old_tail", "truncated_hash_tail", "short_transport", "oversized",
    "invalid_project", "invalid_magic", "invalid_chip", "unterminated_version", "pending_candidate_rejected",
    "prefix_write_error", "body_write_error", "body_timeouts", "end_validation_error", "invalid_final_descriptor",
    "boot_selection_error", "boot_selection_and_journal_error", "journal_error_no_boot_change", "prefix_timeout_progress", "version_json_escaped",
    "power_cut_before_selection", "power_cut_after_selection", "status_failure_truth", "status_persistence_error",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=ROOT)
    args = parser.parse_args()
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not compiler or not cc:
        raise SystemExit("Host C and C++ compilers are required")
    source = (args.source_root / "components/factory/FactoryApi.cpp").read_text()
    start = source.index("esp_err_t post_ota(")
    end = source.index("esp_err_t get_recovery_status(", start)
    harness = HARNESS.replace("__HANDLERS__", source[start:end])
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    with tempfile.TemporaryDirectory(prefix="lumigrid-ota-upload-") as directory:
        build = Path(directory)
        for path, content in STUBS.items():
            target = build / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        (build / "harness.cpp").write_text(harness)
        subprocess.run([cc, "-c", str(cjson / "cJSON.c"), "-I", str(cjson), "-o", str(build / "cjson.o")], check=True)
        root = args.source_root
        executable = build / "ota_upload"
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
                        "-I", str(build), "-I", str(root / "components/ota/include"),
                        "-I", str(root / "components/config/include"), "-I", str(cjson),
                        str(root / "components/ota/OtaBootVerifier.cpp"),
                        str(build / "harness.cpp"), str(build / "cjson.o"), "-o", str(executable)], check=True)
        environment = dict(os.environ)
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        failures = []
        for case in CASES:
            result = subprocess.run([str(executable), case], capture_output=True, text=True, cwd=build, env=environment)
            print(result.stdout or result.stderr, end="")
            if result.returncode:
                failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(CASES)} OTA upload scenarios failed: {', '.join(failures)}")
        print(f"PASS: {len(CASES)} production OTA upload/status scenarios (ASan/UBSan)")


if __name__ == "__main__":
    main()
