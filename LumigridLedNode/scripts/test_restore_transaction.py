"""Run the production restore handler with deterministic storage failpoints.

Package parsing and low-level NVS persistence have independent production-source
tests. This harness replaces those boundaries to exercise the handler's snapshot,
readback, partial-save rollback, marker and job-result decisions. It does not
claim that several NVS keys form an atomic transaction across a power loss.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>
#include "SignageTypes.hpp"
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101,
    ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
    ESP_ERR_NOT_FOUND=0x105, ESP_ERR_NVS_NOT_FOUND=0x1102;
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(unsigned) {}
int reboots=0;
void esp_restart() { ++reboots; }
struct httpd_req_t { void* user_ctx=nullptr; std::string status,output; bool authorized=true; };
int httpd_resp_set_status(httpd_req_t* r,const char* s) { r->status=s; return ESP_OK; }
int httpd_resp_set_type(httpd_req_t*,const char*) { return ESP_OK; }
int httpd_resp_sendstr(httpd_req_t* r,const char* s) { r->output=s; return ESP_OK; }
namespace lg {
enum class MaintenanceMode { None, Busy };
enum class CommandDomain { Restore };
enum class JobType { Restore };
enum class JobState { Preparing, Running, Verifying };
struct MemoryAdmissionResult { bool accepted=false; unsigned projected_free_bytes=0,required_bytes=0,reserve_bytes=0; };
struct RuntimeMemorySnapshot { unsigned free_8bit=1024*1024,largest_8bit=1024*1024; };
RuntimeMemorySnapshot memory_snapshot;
RuntimeMemorySnapshot capture_runtime_memory() { return memory_snapshot; }
constexpr uint32_t kPostTopologyHeapReserveBytes=16*1024;
__MEMORY_ADMISSION__
void note_api_allocation_failure() {}
int send_memory_admission_error(httpd_req_t* r,size_t) { r->status="507 Insufficient Storage"; return ESP_OK; }
int send_error(httpd_req_t* r,const char* status,const char* code) { r->status=status; r->output=code; return ESP_OK; }
int send_json(httpd_req_t* r,const char* body) { r->status="200 OK"; r->output=body; return ESP_OK; }
namespace api {
enum class ApiRole { Admin };
bool authorize(httpd_req_t* r,ApiRole) { return r->authorized; }
struct AuditLog { static AuditLog& instance(){ static AuditLog value; return value; } void log(ApiRole,const char*,const char*) {} };
}
class ConfigStore {
public:
    std::map<std::string,std::vector<uint8_t>> records;
    std::map<std::string,int> reads,writes,deletes;
    std::map<std::pair<std::string,int>,int> read_errors,write_errors,delete_errors;
    std::set<std::pair<std::string,int>> partial_writes,ignored_writes;
    bool marker=false;
    uint32_t marker_checksum=0;
    int marker_reads=0,marker_sets=0,marker_clears=0;
    std::map<int,int> marker_read_errors,marker_set_errors,marker_clear_errors;
    template<class T> void seed(const std::string& key,const T& value) {
        const auto* p=reinterpret_cast<const uint8_t*>(&value); records[key]={p,p+sizeof(T)};
    }
    template<class T> int read(const std::string& key,T& value,bool* present=nullptr,bool defaults=false) {
        const auto op=std::make_pair(key,++reads[key]); if(present) *present=false;
        if(read_errors.count(op)) return read_errors.at(op);
        const auto found=records.find(key);
        if(found==records.end()) { if(defaults) { value=T{}; return ESP_OK; } return ESP_ERR_NVS_NOT_FOUND; }
        assert(found->second.size()==sizeof(T)); std::memcpy(&value,found->second.data(),sizeof(T));
        if(present) *present=true; return ESP_OK;
    }
    template<class T> int write(const std::string& key,const T& value) {
        const auto op=std::make_pair(key,++writes[key]);
        const int error=write_errors.count(op) ? write_errors.at(op) : ESP_OK;
        if((error==ESP_OK || partial_writes.count(op)) && !ignored_writes.count(op)) seed(key,value);
        return error;
    }
    int erase(const std::string& key) {
        const auto op=std::make_pair(key,++deletes[key]);
        if(delete_errors.count(op)) return delete_errors.at(op);
        records.erase(key); return ESP_OK;
    }
    static int validate_config(const DeviceConfig& cfg) { return cfg.gamma_x10>=10 && cfg.gamma_x10<=40 ? ESP_OK : ESP_ERR_INVALID_ARG; }
    int load_config(DeviceConfig& v,bool persist_changes=true) { (void)persist_changes; return read("config",v); }
    int save_config(const DeviceConfig& v) { return write("config",v); }
    int load_scene(uint8_t i,SceneConfig& v,bool persist_changes=true) { (void)persist_changes; return read("scene"+std::to_string(i),v); }
    int save_scene(uint8_t i,const SceneConfig& v) { return write("scene"+std::to_string(i),v); }
    int delete_scene(uint8_t i) { int e=erase("scene"+std::to_string(i)); return e==ESP_OK ? erase("motion"+std::to_string(i)) : e; }
    int load_scene_motion(uint8_t i,SceneMotionConfig& v) { return read("motion"+std::to_string(i),v); }
    int save_scene_motion(uint8_t i,const SceneMotionConfig& v) { return write("motion"+std::to_string(i),v); }
    int delete_scene_motion(uint8_t i) { return erase("motion"+std::to_string(i)); }
    int load_cue_list(uint8_t i,CueList& v) { return read("cue"+std::to_string(i),v); }
    int save_cue_list(uint8_t i,const CueList& v) { return write("cue"+std::to_string(i),v); }
    int delete_cue_list(uint8_t i) { return erase("cue"+std::to_string(i)); }
    int load_sequence(Sequence& v,bool* present=nullptr,bool persist_changes=true) { (void)persist_changes; return read("timeline",v,present,true); }
    int save_sequence(const Sequence& v) { auto copy=v; copy.enabled=false; return write("timeline",copy); }
    int delete_sequence() { return erase("timeline"); }
    int load_calendar_schedule(CalendarSchedule& v,bool* present=nullptr) { return read("calendar",v,present,true); }
    int save_calendar_schedule(const CalendarSchedule& v) { return write("calendar",v); }
    int delete_calendar_schedule() { return erase("calendar"); }
    int read_restore_marker(bool& active,uint32_t* checksum=nullptr) {
        ++marker_reads; active=true; if(checksum) *checksum=0;
        if(marker_read_errors.count(marker_reads)) return marker_read_errors.at(marker_reads);
        active=marker; if(checksum) *checksum=marker_checksum; return ESP_OK;
    }
    int set_restore_in_progress(bool active,uint32_t checksum=0) {
        if(active) {
            ++marker_sets;
            if(marker_set_errors.count(marker_sets)) return marker_set_errors.at(marker_sets);
            marker=true; marker_checksum=checksum;
        } else {
            ++marker_clears; marker=false; marker_checksum=0;
            if(marker_clear_errors.count(marker_clears)) return marker_clear_errors.at(marker_clears);
        }
        return ESP_OK;
    }
    int mutations() const { int n=0; for(auto p:writes)n+=p.second; for(auto p:deletes)n+=p.second; return n; }
};
struct Journal {
    int applied_count=0,failed_count=0,error=0;
    uint32_t accept(CommandDomain) { return 41; }
    void failed(uint32_t,int e) { ++failed_count; error=e; }
    void applied(uint32_t) { ++applied_count; }
};
struct Jobs {
    int completed=0,failed=0,error=0; std::string detail;
    int start_job(JobType,uint32_t* id) { *id=7; return ESP_OK; }
    void update_progress(uint32_t,JobState,unsigned) {}
    void fail_job(uint32_t,int e,const char* why) { ++failed; error=e; detail=why; }
    void complete_job(uint32_t) { ++completed; }
};
class FactoryApi {
public:
    ConfigStore storage; Journal journal; Jobs jobs;
    ConfigStore* config() { return &storage; }
    MaintenanceMode current_maintenance_mode() const { return MaintenanceMode::None; }
    Journal& command_journal() { return journal; }
    Jobs& job_manager() { return jobs; }
    void notify_status_async() {}
};
__PACKAGE__
ParsedBackupPackage incoming;
int ingest_backup_package(httpd_req_t*,ParsedBackupPackage& pkg,ConfigStore*) { pkg=incoming; return ESP_OK; }
static bool backup_record_missing(int e) { return e==ESP_ERR_NOT_FOUND || e==ESP_ERR_NVS_NOT_FOUND; }
__RESTORE__
}

int main(int argc,char** argv) {
    using namespace lg;
    assert(argc==2); const std::string name=argv[1];
    FactoryApi api; auto& store=api.storage; httpd_req_t request; request.user_ctx=&api;
    DeviceConfig cfg{}; std::strcpy(cfg.serial_number,"THIS-BOARD"); std::strcpy(cfg.hardware_revision,"rev-actual");
    std::strcpy(cfg.admin_token,"local-admin"); std::strcpy(cfg.installer_token,"local-installer");
    std::strcpy(cfg.operator_token,"local-operator"); cfg.sync_token=0x12345678;
    SceneConfig old_scene{}; old_scene.global_brightness=27;
    SceneMotionConfig old_motion{}; old_motion.transition_curve=TransitionCurve::Linear;
    CueList old_cue{}; std::strcpy(old_cue.name,"Original");
    Sequence old_seq{}; for(unsigned i=0;i<16;++i) old_seq.tracks[i].channel_id=i; old_seq.bpm=101;
    CalendarSchedule old_cal{}; old_cal.entries[0].hour=8;
    store.seed("config",cfg); store.seed("scene0",old_scene); store.seed("scene1",old_scene);
    store.seed("motion0",old_motion); store.seed("cue0",old_cue);
    store.seed("timeline",old_seq); store.seed("calendar",old_cal);
    store.records["auth"]={1,2,3}; store.records["network"]={4,5,6};
    incoming.checksum=0x87654321;
    incoming.scenes[0].global_brightness=201;
    incoming.motions[0].transition_curve=TransitionCurve::EaseInOut;
    std::strcpy(incoming.cue_lists[0].name,"Replacement");
    incoming.timeline=old_seq; incoming.timeline.bpm=151;
    incoming.calendar.entries[0].hour=20;
    incoming.device_config=cfg; std::strcpy(incoming.device_config.device_name,"Restored");
    std::strcpy(incoming.device_config.serial_number,"OTHER-BOARD");
    std::strcpy(incoming.device_config.hardware_revision,"other-rev");
    std::strcpy(incoming.device_config.admin_token,"remote-admin");
    std::strcpy(incoming.device_config.installer_token,"remote-installer");
    std::strcpy(incoming.device_config.operator_token,"remote-operator"); incoming.device_config.sync_token=99;
    auto include=[&](const std::string& type) {
        if(type=="config") incoming.has_device_config=true;
        if(type=="scene0") incoming.has_scenes[0]=true;
        if(type=="motion0") incoming.has_motions[0]=true;
        if(type=="cue0") incoming.has_cue_lists[0]=true;
        if(type=="timeline") incoming.has_timeline=true;
        if(type=="calendar") incoming.has_calendar=true;
    };
    const std::vector<std::string> types={"scene0","motion0","cue0","timeline","calendar","config"};
    if(name.rfind("snapshot_",0)==0) {
        const std::string type=name.substr(9); include(type); store.read_errors[{type,1}]=ESP_FAIL;
        post_restore(&request); assert(store.mutations()==0 && store.marker_sets==0 && api.jobs.failed==1);
    } else if(name.rfind("verify_",0)==0) {
        const std::string type=name.substr(7); include(type); store.ignored_writes.insert({type,1});
        const auto original=store.records; post_restore(&request);
        assert(api.jobs.failed==1 && api.jobs.completed==0 && reboots==0 && store.records==original);
    } else if(name=="unaffected_not_read") {
        include("scene0"); store.read_errors[{"scene1",1}]=ESP_FAIL; post_restore(&request);
        assert(api.jobs.completed==1 && store.reads["scene1"]==0 && store.reads["config"]==0);
    } else if(name=="marker_read_error") {
        include("scene0"); store.marker_read_errors[1]=ESP_FAIL; post_restore(&request);
        assert(store.mutations()==0 && store.marker_sets==0 && api.jobs.failed==1);
    } else if(name=="active_marker") {
        // An interrupted prior restore needs explicit recovery/recommissioning;
        // applying a partial package cannot prove the unknown earlier state.
        include("scene0"); store.marker=true; store.marker_checksum=123; post_restore(&request);
        assert(store.mutations()==0 && store.marker && store.marker_checksum==123 && api.jobs.failed==1);
        assert(request.output.find("restore_recovery_required")!=std::string::npos);
    } else if(name=="marker_arm_verify_error") {
        include("scene0"); store.marker_read_errors[2]=ESP_FAIL; post_restore(&request);
        assert(store.mutations()==0 && store.marker && api.jobs.failed==1 && reboots==0);
    } else if(name=="orphan_motion_snapshot_error") {
        store.records.erase("scene0"); include("scene0"); store.read_errors[{"motion0",1}]=ESP_FAIL;
        post_restore(&request); assert(store.mutations()==0 && store.marker_sets==0 && api.jobs.failed==1);
    } else if(name=="marker_set_error") {
        include("scene0"); store.marker_set_errors[1]=ESP_FAIL; post_restore(&request);
        assert(store.mutations()==0 && api.jobs.failed==1 && reboots==0);
    } else if(name=="invalid_final_config") {
        include("config"); incoming.device_config.gamma_x10=1; post_restore(&request);
        assert(store.mutations()==0 && store.marker_sets==0 && api.jobs.failed==1);
    } else if(name=="rollback_touched_only" || name=="rollback_partial_write") {
        include("scene0"); include("cue0"); store.write_errors[{"scene0",1}]=ESP_FAIL;
        store.partial_writes.insert({"scene0",1}); const auto original=store.records;
        post_restore(&request); assert(store.records==original && !store.marker && api.jobs.failed==1);
        assert(store.writes.size()==1 && store.writes["scene0"]==2 && store.deletes.empty());
    } else if(name=="rollback_failed" || name=="rollback_mismatch") {
        include("scene0"); store.write_errors[{"scene0",1}]=ESP_FAIL; store.partial_writes.insert({"scene0",1});
        if(name=="rollback_failed") store.write_errors[{"scene0",2}]=ESP_FAIL;
        else store.ignored_writes.insert({"scene0",2});
        post_restore(&request); assert(store.marker && api.jobs.failed==1 && reboots==0);
        assert(request.output.find("rollback_incomplete")!=std::string::npos);
    } else if(name=="rollback_absent_scene_motion") {
        store.records.erase("scene0"); include("scene0"); include("cue0");
        store.write_errors[{"cue0",1}]=ESP_FAIL; const auto original=store.records;
        post_restore(&request); assert(store.records==original && !store.marker && api.jobs.failed==1);
    } else if(name=="rollback_absent_timeline") {
        store.records.erase("timeline"); include("timeline"); include("calendar");
        store.write_errors[{"calendar",1}]=ESP_FAIL; const auto original=store.records;
        post_restore(&request); assert(store.records==original && !store.marker && api.jobs.failed==1);
    } else if(name=="rollback_absent_calendar") {
        store.records.erase("calendar"); include("calendar"); include("config");
        store.write_errors[{"config",1}]=ESP_FAIL; const auto original=store.records;
        post_restore(&request); assert(store.records==original && !store.marker && api.jobs.failed==1);
    } else if(name=="rollback_delete_error") {
        store.records.erase("scene0"); include("scene0"); store.write_errors[{"scene0",1}]=ESP_FAIL;
        store.partial_writes.insert({"scene0",1}); store.delete_errors[{"scene0",1}]=ESP_FAIL;
        post_restore(&request); assert(store.marker && api.jobs.failed==1 && reboots==0);
        assert(request.output.find("rollback_incomplete")!=std::string::npos);
    } else if(name=="marker_clear_error" || name=="marker_clear_verify_error" || name=="rollback_marker_clear_error") {
        include("scene0");
        if(name=="marker_clear_verify_error") store.marker_read_errors[3]=ESP_FAIL;
        else store.marker_clear_errors[1]=ESP_FAIL;
        if(name=="rollback_marker_clear_error") {
            store.write_errors[{"scene0",1}]=ESP_FAIL; store.partial_writes.insert({"scene0",1});
        }
        post_restore(&request);
        assert(store.marker && api.jobs.failed==1 && api.jobs.completed==0 && reboots==0);
        assert(request.output.find("marker_clear_failed")!=std::string::npos);
    } else if(name=="identity_preserved") {
        include("config"); const auto auth=store.records.at("auth"),net=store.records.at("network");
        post_restore(&request); DeviceConfig loaded{}; assert(store.load_config(loaded)==ESP_OK);
        assert(std::strcmp(loaded.serial_number,cfg.serial_number)==0);
        assert(std::strcmp(loaded.hardware_revision,cfg.hardware_revision)==0);
        assert(std::strcmp(loaded.admin_token,cfg.admin_token)==0 && std::strcmp(loaded.installer_token,cfg.installer_token)==0);
        assert(std::strcmp(loaded.operator_token,cfg.operator_token)==0 && loaded.sync_token==cfg.sync_token);
        assert(store.records.at("auth")==auth && store.records.at("network")==net && api.jobs.completed==1);
    } else if(name=="memory_exact_budget" || name=="memory_reserve_rejected" || name=="memory_largest_rejected") {
        include("scene0");
        const size_t total=sizeof(ParsedBackupPackage)+sizeof(RestoreRollbackSnapshot)+
                           sizeof(RestoreVerifyScratch)+kMaxRestoreRecordSize+32768u;
        memory_snapshot.free_8bit=total+kPostTopologyHeapReserveBytes;
        memory_snapshot.largest_8bit=std::max(sizeof(ParsedBackupPackage),sizeof(RestoreRollbackSnapshot));
        if(name=="memory_reserve_rejected") --memory_snapshot.free_8bit;
        if(name=="memory_largest_rejected") --memory_snapshot.largest_8bit;
        post_restore(&request);
        if(name=="memory_exact_budget") assert(api.jobs.completed==1);
        else assert(store.mutations()==0 && store.marker_sets==0 && request.status=="507 Insufficient Storage");
    } else if(name=="all_success") {
        for(const auto& type:types) include(type); post_restore(&request);
        assert(api.jobs.completed==1 && api.jobs.failed==0 && reboots==1 && !store.marker);
        assert(api.journal.applied_count==1);
        for(const auto& type:types) assert(store.reads[type]>=2 && store.writes[type]==1);
    } else { assert(false && "unknown restore case"); }
    std::printf("[PASS] restore transaction: %s\n",name.c_str());
}
'''

CASES = (
    *(f"snapshot_{name}" for name in ("scene0", "motion0", "cue0", "timeline", "calendar", "config")),
    *(f"verify_{name}" for name in ("scene0", "motion0", "cue0", "timeline", "calendar", "config")),
    "unaffected_not_read", "marker_read_error", "active_marker", "marker_set_error", "invalid_final_config",
    "rollback_touched_only", "rollback_partial_write", "rollback_failed", "rollback_mismatch",
    "rollback_absent_scene_motion", "rollback_absent_timeline", "rollback_absent_calendar", "rollback_delete_error",
    "marker_clear_error", "identity_preserved", "all_success",
    "memory_exact_budget", "memory_reserve_rejected", "memory_largest_rejected",
    "marker_arm_verify_error", "orphan_motion_snapshot_error", "marker_clear_verify_error", "rollback_marker_clear_error",
)


def main():
    source = (ROOT / "components/factory/FactoryApi.cpp").read_text()
    package = source[source.index("struct ParsedBackupPackage"):
                     source.index("static esp_err_t ingest_backup_package(")]
    restore = source[source.index("struct RestoreRollbackSnapshot"):
                     source.index("// ── Certification Handlers")]
    memory = (ROOT / "components/factory/include/RuntimeMemory.hpp").read_text()
    memory = memory[memory.index("inline MemoryAdmissionResult check_memory_admission_snapshot("):
                    memory.index("inline MemoryAdmissionResult check_memory_admission(\n")]
    harness = HARNESS.replace("__PACKAGE__", package).replace("__RESTORE__", restore).replace("__MEMORY_ADMISSION__", memory)
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-restore-transaction-") as directory:
        temp = Path(directory)
        (temp / "driver").mkdir()
        (temp / "driver/gpio.h").write_text("#pragma once\nusing gpio_num_t=int;\n" +
            "\n".join(f"constexpr int GPIO_NUM_{n}={n};" for n in range(40)))
        (temp / "harness.cpp").write_text(harness)
        executable = temp / "harness"
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-Wno-misleading-indentation",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I", str(temp), "-I", str(ROOT / "components/config/include"),
                        "-I", str(ROOT / "components/board/include"),
                        str(temp / "harness.cpp"), "-o", str(executable)], check=True)
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        failures = []
        cases = sys.argv[1:] or CASES
        for case in cases:
            result = subprocess.run([str(executable), case], env=env, capture_output=True, text=True, timeout=10)
            print(result.stdout, end="")
            if result.returncode:
                failures.append(case)
                print(f"[FAIL] {case}: {result.stderr.strip()}")
        print(f"Restore transaction: {len(cases)-len(failures)} passed; failures={failures}")
        return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
