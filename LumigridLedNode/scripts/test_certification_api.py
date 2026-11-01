"""Exercise the real certification result/abort HTTP handlers on the host."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include "cJSON.h"
#include "CertificationPlan.hpp"
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101,
              ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
              ESP_ERR_NOT_FOUND=0x105;
struct httpd_req_t { void* user_ctx=nullptr; std::string input, status, output; int content_len=0; };
constexpr int64_t fake_time_us=5000000000000LL;
int64_t esp_timer_get_time() { return fake_time_us; }
namespace lg {
namespace api { enum class ApiRole { Admin }; bool authorize(httpd_req_t*,ApiRole) { return true; } }
struct FakeService {
    CertificationSession value{};
    CertificationTestResult test{};
    int result_calls=0, abort_calls=0, result_error=ESP_OK, abort_error=ESP_OK;
    int start_calls=0, test_start_calls=0, start_error=ESP_OK;
    bool pending_cancellation=false;
    int ticks=0; uint64_t tick_time=0;
    void tick(uint64_t time) { ++ticks; tick_time=time; }
    int record_result(const char*,CertificationTestState,EvidenceSource,float,const char*,const char*) {
        ++result_calls; return result_error;
    }
    int abort() { ++abort_calls; return abort_error; }
    int start_session(CertificationProfile, const char*) { ++start_calls; return start_error; }
    int start_test(const char*) { ++test_start_calls; return start_error; }
    const CertificationTestResult* get_test_result(const char*) { return &test; }
    bool cancellation_pending() const { return pending_cancellation; }
    const CertificationSession& session() { return value; }
};
class FactoryApi { public:
    FakeService service; int notifications=0;
    FakeService* cert_service_=&service;
    FakeService* certification_service() { return cert_service_; }
    void notify_status_async() { ++notifications; }
    void notify_queued_status();
    static void ws_broadcast_work(void*);
    void* factory_server_=this; std::atomic<bool> status_pending_{false};
    uint32_t next_ws_seq() { return 1; }
};
constexpr size_t kMaxClientListFds=8;
constexpr int HTTPD_WS_CLIENT_WEBSOCKET=1, HTTPD_WS_TYPE_TEXT=1;
struct WsBroadcastArg { FactoryApi* api=nullptr; char payload[512]{}; };
struct RuntimeSnapshot {};
struct FixedJsonWriter { FixedJsonWriter(char*,size_t) {} size_t size() { return 0; } };
struct httpd_ws_frame_t { int type=0; uint8_t* payload=nullptr; size_t len=0; };
int clients_to_return=0, client_kind=0, queue_error=ESP_OK;
void (*queued_work)(void*)=nullptr; void* queued_arg=nullptr;
int httpd_get_client_list(void*,size_t* count,int* fds) { *count=clients_to_return; if(*count) fds[0]=1; return ESP_OK; }
int httpd_ws_get_fd_info(void*,int) { return client_kind; }
int httpd_ws_send_frame_async(void*,int,httpd_ws_frame_t*) { return ESP_OK; }
RuntimeSnapshot collect_runtime_snapshot(FactoryApi* api,uint32_t) {
    assert(api->service.ticks==1); return {};
}
bool serialize_runtime_snapshot(const RuntimeSnapshot&,FixedJsonWriter&) { return true; }
void note_api_allocation_failure() {}
int httpd_queue_work(void*,void (*work)(void*),void* arg) {
    if(queue_error!=ESP_OK) return queue_error;
    assert(!queued_work); queued_work=work; queued_arg=arg; return ESP_OK;
}
int read_json(httpd_req_t* request,cJSON** body) {
    *body=cJSON_Parse(request->input.c_str()); return *body ? ESP_OK : ESP_FAIL;
}
int send_error(httpd_req_t* request,const char* status,const char* error) {
    request->status=status; request->output=error; return ESP_OK;
}
int send_json(httpd_req_t* request,cJSON* body,const char* status) {
    request->status=status; char* text=cJSON_PrintUnformatted(body);
    request->output=text ? text : ""; cJSON_free(text); cJSON_Delete(body); return ESP_OK;
}
__HANDLERS__
__WORKERS__
}
int main(int argc,char** argv) {
    using namespace lg;
    assert(argc==2); std::string name=argv[1]; FactoryApi api;
    if(name.rfind("dispatch_",0)==0) {
        if(name=="dispatch_non_ws") clients_to_return=1;
        else if(name=="dispatch_ws") { clients_to_return=1; client_kind=HTTPD_WS_CLIENT_WEBSOCKET; }
        else if(name=="dispatch_no_service") api.cert_service_=nullptr;
        else if(name=="dispatch_queue_failure") {
            queue_error=ESP_FAIL;
            api.notify_queued_status();
            assert(!api.status_pending_ && !queued_work && api.service.ticks==0);
            queue_error=ESP_OK;
        } else assert(name=="dispatch_no_clients");
        api.notify_queued_status(); api.notify_queued_status();
        assert(queued_work && api.status_pending_ && api.service.ticks==0);
        auto* work=queued_work; auto* arg=queued_arg; queued_work=nullptr; queued_arg=nullptr;
        work(arg);
        assert(!api.status_pending_);
        if(api.cert_service_) assert(api.service.ticks==1 && api.service.tick_time==fake_time_us/1000);
        else assert(api.service.ticks==0);
        std::cout << "PASS " << name << '\n'; return 0;
    }
    httpd_req_t request; request.user_ctx=&api;
    std::strcpy(api.service.value.identity.certification_id,"cert-bound");
    api.service.test.started_ms=1234;
    api.service.test.attempts=2;
    if(name.rfind("session_",0)==0 || name.rfind("test_start_",0)==0 || name=="abort_stale_session" || name=="abort_empty_body") {
        request.input=R"({"profile":"development","serialNumber":"bench","certificationId":"cert-bound","testId":"POWER-LIMIT-001"})";
        if(name=="session_invalid_json") request.input="{";
        else if(name=="session_not_object") request.input="[]";
        else if(name=="session_invalid_profile") request.input=R"({"profile":"anything","serialNumber":"bench"})";
        else if(name=="session_invalid_serial") request.input=R"({"profile":"development","serialNumber":123})";
        else if(name=="session_oversize_serial") request.input=R"({"serialNumber":"12345678901234567890123456789012"})";
        else if(name=="session_stale_binding" || name=="test_start_stale_binding" || name=="abort_stale_session")
            request.input=R"({"profile":"development","certificationId":"old","testId":"POWER-LIMIT-001"})";
        else if(name=="session_storage_failure") api.service.start_error=ESP_ERR_NO_MEM;
        else if(name=="test_start_storage_failure") api.service.start_error=ESP_FAIL;
        else if(name=="abort_empty_body") request.input="";
        else assert(name=="session_bound" || name=="test_start_bound");
        request.content_len=request.input.size();
        if(name.rfind("session_",0)==0) assert(post_certification_start(&request)==ESP_OK);
        else if(name.rfind("test_start_",0)==0) assert(post_certification_test_start(&request)==ESP_OK);
        else assert(post_certification_abort(&request)==ESP_OK);
        const int mutations=api.service.start_calls+api.service.test_start_calls+api.service.abort_calls;
        const bool stale=name.find("stale")!=std::string::npos;
        const bool storage=name.find("storage_failure")!=std::string::npos;
        const bool valid=name=="session_bound" || name=="test_start_bound" || name=="abort_empty_body";
        if(stale) assert(request.status=="409 Conflict" && mutations==0 && api.notifications==0);
        else if(storage) {
            assert(request.status==(name=="session_storage_failure" ? "503 Service Unavailable" : "500 Internal Server Error"));
            assert(mutations==1 && api.notifications==0);
        } else if(valid) assert(request.status=="200 OK" && mutations==1 && api.notifications==1);
        else assert(request.status=="400 Bad Request" && mutations==0 && api.notifications==0);
        assert(api.service.ticks==1);
        std::cout << "PASS " << name << '\n'; return 0;
    }
    std::string body=R"({"testId":"POWER-LIMIT-001","state":"passed","evidenceSource":"instrument_measurement","measuredValue":0,"unit":"mW","note":"bench"})";
    cJSON* json=cJSON_Parse(body.c_str()); assert(json);
    bool reject=true;
    bool conflict=false;
    if(name.rfind("binding_",0)==0) {
        cJSON_AddStringToObject(json,"certificationId",name=="binding_stale_session" ? "old" : "cert-bound");
        cJSON_AddNumberToObject(json,"testStartedMs",name=="binding_old_start" ? 1233 : name=="binding_fractional_start" ? 1234.5 : 1234);
        cJSON_AddNumberToObject(json,"testAttempt",name=="binding_old_attempt" ? 1 : name=="binding_invalid_attempt" ? 256 : 2);
        if(name=="binding_missing_start") cJSON_DeleteItemFromObjectCaseSensitive(json,"testStartedMs");
        else if(name=="binding_missing_session") cJSON_DeleteItemFromObjectCaseSensitive(json,"certificationId");
        else if(name=="binding_current") reject=false;
        else if(name=="binding_stale_session" || name=="binding_old_start" || name=="binding_old_attempt") conflict=true;
        else assert(name=="binding_fractional_start" || name=="binding_invalid_attempt");
    } else if(name=="missing_measurement") cJSON_DeleteItemFromObjectCaseSensitive(json,"measuredValue");
    else if(name=="non_numeric_measurement") cJSON_ReplaceItemInObjectCaseSensitive(json,"measuredValue",cJSON_CreateString("0"));
    else if(name=="wrong_state") cJSON_ReplaceItemInObjectCaseSensitive(json,"state",cJSON_CreateString("oops"));
    else if(name=="nonterminal_state") cJSON_ReplaceItemInObjectCaseSensitive(json,"state",cJSON_CreateString("running"));
    else if(name=="wrong_source") cJSON_ReplaceItemInObjectCaseSensitive(json,"evidenceSource",cJSON_CreateString("oops"));
    else if(name=="external_automatic") {
        cJSON_ReplaceItemInObjectCaseSensitive(json,"testId",cJSON_CreateString("BOOT-001"));
        cJSON_ReplaceItemInObjectCaseSensitive(json,"evidenceSource",cJSON_CreateString("automatic"));
    } else if(name=="automatic_observation") {
        cJSON_ReplaceItemInObjectCaseSensitive(json,"testId",cJSON_CreateString("BOOT-001"));
        cJSON_ReplaceItemInObjectCaseSensitive(json,"evidenceSource",cJSON_CreateString("operator_observation"));
    } else if(name=="long_note") cJSON_ReplaceItemInObjectCaseSensitive(json,"note",cJSON_CreateString(std::string(96,'x').c_str()));
    else if(name=="utf8_note_at_limit") { reject=false; cJSON_ReplaceItemInObjectCaseSensitive(json,"note",cJSON_CreateString((std::string(93,'x')+"é").c_str())); }
    else if(name=="utf8_note_over_limit") cJSON_ReplaceItemInObjectCaseSensitive(json,"note",cJSON_CreateString((std::string(94,'x')+"é").c_str()));
    else if(name=="wrong_unit") cJSON_ReplaceItemInObjectCaseSensitive(json,"unit",cJSON_CreateString("V"));
    else if(name=="valid_zero_measurement") reject=false;
    else if(name=="result_storage_failure") { api.service.result_error=ESP_ERR_NO_MEM; reject=false; }
    else if(name=="abort_storage_failure") { api.service.abort_error=ESP_FAIL; }
    else if(name=="abort_pending") { api.service.pending_cancellation=true; }
    else if(name=="abort_completed") {}
    else if(name=="abort_queue_rejected") { api.service.abort_error=ESP_ERR_INVALID_STATE; api.service.pending_cancellation=true; }
    else if(name!="float_overflow" && name!="double_overflow") assert(false);
    char* serialized=cJSON_PrintUnformatted(json); request.input=serialized; cJSON_free(serialized); cJSON_Delete(json);
    if(name=="float_overflow" || name=="double_overflow")
        request.input=std::string(R"({"testId":"POWER-LIMIT-001","state":"passed","evidenceSource":"instrument_measurement","measuredValue":)")+
            (name=="float_overflow" ? "1e50" : "1e400")+R"(,"unit":"mW","note":"bench"})";
    request.content_len=request.input.size();
    if(name.rfind("abort_",0)==0) {
        assert(post_certification_abort(&request)==ESP_OK);
        assert(api.service.abort_calls==1);
        if(name=="abort_storage_failure") {
            assert(request.status=="500 Internal Server Error" && api.notifications==0);
        } else if(name=="abort_queue_rejected") {
            assert(request.status=="409 Conflict" && api.notifications==0);
        } else {
            assert(api.notifications==1);
            cJSON* reply=cJSON_Parse(request.output.c_str()); assert(reply);
            const cJSON* status=cJSON_GetObjectItemCaseSensitive(reply,"status");
            assert(cJSON_IsString(status));
            if(name=="abort_pending") {
                assert(request.status=="202 Accepted");
                assert(strcmp(status->valuestring,"cancellation_requested")==0);
                assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"accepted")));
                assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"cancellationRequested")));
            } else {
                assert(request.status=="200 OK" && strcmp(status->valuestring,"aborted")==0);
            }
            cJSON_Delete(reply);
        }
    } else {
        assert(post_certification_test_result(&request)==ESP_OK);
        if(name=="result_storage_failure") {
            assert(request.status=="503 Service Unavailable" && api.service.result_calls==1 && api.notifications==0);
        } else if(conflict) {
            assert(request.status=="409 Conflict" && api.service.result_calls==0 && api.notifications==0);
        } else if(reject) {
            assert(request.status=="400 Bad Request" && api.service.result_calls==0 && api.notifications==0);
        } else {
            assert(request.status=="200 OK" && api.service.result_calls==1 && api.notifications==1);
        }
    }
    assert(api.service.ticks==1 && api.service.tick_time==fake_time_us/1000);
    std::cout << "PASS " << name << '\n';
}
'''


def main():
    cxx=shutil.which(os.environ.get("CXX","g++"))
    cc=shutil.which(os.environ.get("CC","cc"))
    if not cxx or not cc: raise SystemExit("Host C/C++ compilers are required")
    source=(ROOT/"components/factory/FactoryApi.cpp").read_text()
    start=source.index("static bool certification_binding_matches(")
    end=source.index("static esp_err_t get_certification_report(",start)
    harness=HARNESS.replace("__HANDLERS__",source[start:end])
    worker_start=source.index("void FactoryApi::ws_broadcast_work(")
    worker_end=source.index("void FactoryApi::ws_broadcast_preview_work(",worker_start)
    notify_start=source.index("void FactoryApi::notify_status_async(")
    notify_end=source.index("void FactoryApi::queue_service_reconcile(",notify_start)
    # Preserve the real queue implementation while keeping handler notifications
    # separately observable in this fake HTTP owner.
    workers=source[worker_start:worker_end]+source[notify_start:notify_end].replace(
        "FactoryApi::notify_status_async", "FactoryApi::notify_queued_status")
    harness=harness.replace("__WORKERS__",workers)
    cjson=ROOT/"managed_components/espressif__cjson/cJSON"
    with tempfile.TemporaryDirectory(prefix="lumigrid-certification-api-") as directory:
        build=Path(directory); (build/"harness.cpp").write_text(harness)
        sanitizer_flags=["-fsanitize=address,undefined","-fno-omit-frame-pointer","-fno-pie"]
        subprocess.run([cc,*sanitizer_flags,"-c",str(cjson/"cJSON.c"),"-I",str(cjson),"-o",str(build/"cjson.o")],check=True)
        executable=build/"certification_api"
        subprocess.run([cxx,"-std=c++17","-O1","-Wall","-Wextra",*sanitizer_flags,"-no-pie","-I",str(cjson),
                        "-I",str(ROOT/"components/certification/include"),
                        str(ROOT/"components/certification/CertificationPlan.cpp"),
                        str(build/"harness.cpp"),str(build/"cjson.o"),"-o",str(executable)],check=True)
        cases=("missing_measurement","non_numeric_measurement","wrong_state","nonterminal_state",
               "wrong_source","external_automatic","wrong_unit","float_overflow","double_overflow",
               "valid_zero_measurement","result_storage_failure","abort_storage_failure",
               "abort_pending","abort_completed","abort_queue_rejected",
               "dispatch_no_clients","dispatch_non_ws","dispatch_ws","dispatch_no_service","dispatch_queue_failure",
               "automatic_observation","long_note","utf8_note_at_limit","utf8_note_over_limit",
               "binding_stale_session","binding_old_start","binding_old_attempt","binding_current",
               "binding_fractional_start","binding_invalid_attempt","binding_missing_start","binding_missing_session",
               "session_invalid_json","session_not_object","session_invalid_profile","session_invalid_serial",
               "session_oversize_serial","session_stale_binding","session_bound","session_storage_failure",
               "test_start_stale_binding","test_start_bound","test_start_storage_failure","abort_stale_session","abort_empty_body")
        cases=sys.argv[1:] or cases
        failures=[]
        env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0:halt_on_error=1",
             "UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"}
        for case in cases:
            result=subprocess.run([str(executable),case],capture_output=True,text=True,cwd=build,env=env)
            print(result.stdout or result.stderr,end="")
            if result.returncode: failures.append(case)
        if failures: raise SystemExit(f"{len(failures)}/{len(cases)} API cases failed: {', '.join(failures)}")
        print(f"PASS: {len(cases)} production certification API scenarios")


if __name__=="__main__": main()
