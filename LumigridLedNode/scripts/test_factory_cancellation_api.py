"""Exercise production factory cancellation and ownership admission handlers.

The runner boundary accepts or rejects cancellation without executing its
worker. Responses must describe that pending request, and the command journal
must not report an applied blackout before the worker completes it.
Manual start requests must preserve active or cancelling certification ownership.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CJSON = ROOT / "managed_components/espressif__cjson/cJSON"
SOURCE_PATH = "LumigridLedNode/components/factory/FactoryApi.cpp"

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>
#include "cJSON.h"
#include "cjson_helpers.hpp"
#include "FactoryTypes.hpp"
#include "CommandReceipt.hpp"
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=0x103;
struct httpd_req_t {
    void* user_ctx=nullptr;
    bool authorized=true;
    int auth_calls=0;
    std::string status,output,input;
};
namespace lg {
namespace api {
enum class ApiRole { Installer };
bool authorize(httpd_req_t* request,ApiRole role) {
    ++request->auth_calls;
    assert(role==ApiRole::Installer);
    if(!request->authorized) {
        request->status="403 Forbidden";
        request->output="{\"error\":\"forbidden\"}";
    }
    return request->authorized;
}
}
struct FakeJournal {
    static constexpr uint32_t accepted_id=54321;
    int accepted_calls=0,applied_calls=0,failed_calls=0;
    uint32_t applied_id=0,failed_id=0;
    int failure=0;
    uint32_t accept(CommandDomain domain) {
        assert(domain==CommandDomain::Factory); ++accepted_calls; return accepted_id;
    }
    void applied(uint32_t id) { ++applied_calls; applied_id=id; }
    void failed(uint32_t id,int error) { ++failed_calls; failed_id=id; failure=error; }
};
struct FakeRunner {
    static constexpr uint8_t kBurnInDefaultBrightness=64, kBurnInAbsoluteMaxBrightness=96;
    bool accept_cancellation=true;
    int enqueue_calls=0,legacy_abort_calls=0,session_abort_calls=0,burn_stop_calls=0;
    int session_start_calls=0,burn_start_calls=0;
    FactoryCommand queued{};
    // These remain active until a separate worker performs the cancellation.
    bool output_active=true,burn_active=true;
    FactoryTestState session_state=FactoryTestState::Running;
    bool enqueue(const FactoryCommand& command) {
        ++enqueue_calls; queued=command; return accept_cancellation;
    }
    bool abort() { ++legacy_abort_calls; return accept_cancellation; }
    bool abort_test_session(uint32_t session_id=0) {
        assert(session_id==0); ++session_abort_calls; return accept_cancellation;
    }
    bool stop_burn_in_job() { ++burn_stop_calls; return accept_cancellation; }
    bool start_test_session(FactoryTestKind kind,uint8_t channel,uint32_t duration,
                            uint8_t brightness,uint32_t* session_id) {
        assert(kind==FactoryTestKind::AddressableChannel && channel==1);
        assert(duration==5000 && brightness==32);
        ++session_start_calls; *session_id=17; return true;
    }
    bool start_burn_in_job(int* manager,uint32_t duration,uint8_t brightness,uint32_t* job_id) {
        assert(manager && *manager==123 && duration==5 && brightness==64);
        ++burn_start_calls; *job_id=42; return true;
    }
};
using FactoryRunner=FakeRunner;
struct FakeCertificationService {
    bool active=false,pending=false;
    bool is_in_progress() const { return active; }
    bool cancellation_pending() const { return pending; }
};
class FactoryApi {
public:
    FakeRunner runner_; FakeJournal journal_;
    FakeCertificationService certification_;
    bool has_certification=true;
    int jobs_=123;
    FakeRunner& runner() { return runner_; }
    FakeJournal& command_journal() { return journal_; }
    int& job_manager() { return jobs_; }
    FakeCertificationService* certification_service() { return has_certification ? &certification_ : nullptr; }
};
int read_json(httpd_req_t* request,cJSON** result) {
    *result=cJSON_Parse(request->input.c_str());
    return *result ? ESP_OK : ESP_ERR_INVALID_STATE;
}
bool json_u32(const cJSON* root,const char* key,uint32_t minimum,uint32_t maximum,uint32_t& value) {
    uint32_t parsed=0;
    if(!json::get_number(root,key,parsed) || parsed<minimum || parsed>maximum) return false;
    value=parsed; return true;
}
bool json_u8(const cJSON* root,const char* key,uint8_t minimum,uint8_t maximum,uint8_t& value) {
    uint32_t parsed=0;
    if(!json_u32(root,key,minimum,maximum,parsed)) return false;
    value=static_cast<uint8_t>(parsed); return true;
}
int send_json(httpd_req_t* request,const char* body,const char* status="200 OK") {
    request->status=status; request->output=body; return ESP_OK;
}
int send_error(httpd_req_t* request,const char* status,const char* code) {
    request->status=status;
    request->output=std::string("{\"error\":\"")+code+"\"}";
    return ESP_OK;
}
__HANDLERS__
}

int main(int argc,char** argv) {
    using namespace lg;
    assert(argc==2); const std::string name=argv[1];
    const size_t separator=name.find('_'); assert(separator!=std::string::npos);
    const std::string endpoint=name.substr(0,separator),mode=name.substr(separator+1);
    FactoryApi api; httpd_req_t request;
    if(endpoint=="startsession" || endpoint=="startburn") {
        assert(mode=="active" || mode=="pending" || mode=="idle" || mode=="absent");
        request.user_ctx=&api;
        api.certification_.active=mode=="active";
        api.certification_.pending=mode=="pending";
        api.has_certification=mode!="absent";
        request.input=endpoint=="startsession" ?
            R"({"kind":"addressable_channel","channel":1,"durationMs":5000,"brightness":32})" :
            R"({"durationMinutes":5,"brightness":64})";
        const esp_err_t result=endpoint=="startsession" ? post_factory_test_start(&request) :
            post_factory_burn_in_start(&request);
        assert(result==ESP_OK && request.auth_calls==1);
        cJSON* body=cJSON_Parse(request.output.c_str()); assert(body);
        const bool blocked=mode=="active" || mode=="pending";
        if(blocked) {
            assert(request.status=="409 Conflict");
            assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"accepted")));
            assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(body,"error")));
            assert(api.runner_.session_start_calls==0 && api.runner_.burn_start_calls==0);
        } else {
            assert(request.status=="202 Accepted");
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"accepted")));
            assert(api.runner_.session_start_calls==(endpoint=="startsession" ? 1 : 0));
            assert(api.runner_.burn_start_calls==(endpoint=="startburn" ? 1 : 0));
            const cJSON* id=cJSON_GetObjectItemCaseSensitive(body,endpoint=="startsession" ? "sessionId" : "jobId");
            assert(cJSON_IsNumber(id) && id->valuedouble==(endpoint=="startsession" ? 17 : 42));
        }
        assert(api.runner_.enqueue_calls==0 && api.runner_.legacy_abort_calls==0);
        assert(api.runner_.session_abort_calls==0 && api.runner_.burn_stop_calls==0);
        assert(api.journal_.accepted_calls==0 && api.journal_.failed_calls==0 && api.journal_.applied_calls==0);
        assert(api.certification_.active==(mode=="active") && api.certification_.pending==(mode=="pending"));
        cJSON_Delete(body);
        std::printf("[PASS] factory start ownership API: %s\n",name.c_str());
        return 0;
    }
    assert(mode=="pending" || mode=="rejected" || mode=="unauthorized");
    request.authorized=mode!="unauthorized";
    // Unauthorized handlers must return before inspecting any application owner.
    request.user_ctx=request.authorized ? &api : nullptr;
    api.runner_.accept_cancellation=mode!="rejected";
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if(endpoint=="abort") result=post_abort(&request);
    else if(endpoint=="session") result=post_factory_test_abort(&request);
    else if(endpoint=="burn") result=post_factory_burn_in_stop(&request);
    else assert(false && "unknown endpoint");
    assert(result==ESP_OK && request.auth_calls==1);
    const auto& runner=api.runner_;
    const auto& journal=api.journal_;
    assert(journal.applied_calls==0);
    assert(runner.output_active && runner.burn_active && runner.session_state==FactoryTestState::Running);
    cJSON* body=cJSON_Parse(request.output.c_str()); assert(body);
    if(mode=="unauthorized") {
        assert(request.status=="403 Forbidden");
        assert(journal.accepted_calls==0 && journal.failed_calls==0);
        assert(runner.enqueue_calls==0 && runner.legacy_abort_calls==0);
        assert(runner.session_abort_calls==0 && runner.burn_stop_calls==0);
    } else {
        if(endpoint=="abort") {
            assert(journal.accepted_calls==1);
            assert(runner.enqueue_calls==1 && runner.legacy_abort_calls==0);
            assert(runner.queued.type==FactoryCommandType::Abort);
            assert(runner.queued.command_id==FakeJournal::accepted_id);
            assert(runner.session_abort_calls==0 && runner.burn_stop_calls==0);
        } else {
            assert(journal.accepted_calls==0 && journal.failed_calls==0);
            assert(runner.enqueue_calls==0 && runner.legacy_abort_calls==0);
            assert(runner.session_abort_calls==(endpoint=="session" ? 1 : 0));
            assert(runner.burn_stop_calls==(endpoint=="burn" ? 1 : 0));
        }
        if(mode=="pending") {
            assert(request.status=="202 Accepted" && journal.failed_calls==0);
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"accepted")));
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"cancellationRequested")));
            assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"aborted")));
            assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"stopped")));
            if(endpoint=="abort") {
                const cJSON* id=cJSON_GetObjectItemCaseSensitive(body,"commandId");
                assert(cJSON_IsNumber(id) && id->valuedouble==FakeJournal::accepted_id);
            }
        } else {
            assert(request.status=="409 Conflict");
            assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"accepted")));
            const cJSON* error=cJSON_GetObjectItemCaseSensitive(body,"error");
            assert(cJSON_IsString(error));
            assert(std::strcmp(error->valuestring,endpoint=="burn" ? "no_active_burn_in_job" : "command_queue_full")==0);
            if(endpoint=="abort") {
                assert(journal.failed_calls==1 && journal.failed_id==FakeJournal::accepted_id);
                assert(journal.failure==ESP_ERR_INVALID_STATE);
            }
        }
    }
    cJSON_Delete(body);
    std::printf("[PASS] factory cancellation API: %s\n",name.c_str());
}
'''

CASES = tuple(f"{endpoint}_{mode}" for endpoint in ("abort", "session", "burn")
              for mode in ("pending", "rejected", "unauthorized")) + tuple(
                  f"{endpoint}_{mode}" for endpoint in ("startsession", "startburn")
                  for mode in ("active", "pending", "idle", "absent"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", help="Read handlers from a local git revision for baseline reproduction")
    parser.add_argument("cases", nargs="*", choices=CASES)
    args = parser.parse_args()
    if args.source_ref:
        source = subprocess.check_output(["git", "show", f"{args.source_ref}:{SOURCE_PATH}"],
                                         cwd=ROOT.parent, text=True)
    else:
        source = (ROOT.parent / SOURCE_PATH).read_text()
    boundaries = (
        ("esp_err_t post_abort(", "esp_err_t post_auto_loop("),
        ("esp_err_t post_factory_test_abort(", "esp_err_t get_factory_test_report("),
        ("esp_err_t post_factory_burn_in_stop(", "esp_err_t ws_handler("),
        ("esp_err_t post_factory_test_start(", "esp_err_t post_factory_test_confirm("),
        ("esp_err_t post_factory_burn_in_start(", "esp_err_t post_factory_burn_in_stop("),
    )
    handlers = []
    for beginning, ending in boundaries:
        start = source.index(beginning)
        handlers.append(source[start:source.index(ending, start)])
    harness = HARNESS.replace("__HANDLERS__", "\n".join(handlers))
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cxx or not cc:
        raise SystemExit("Host C and C++ compilers are required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-factory-cancellation-api-") as directory:
        temp = Path(directory)
        (temp / "harness.cpp").write_text(harness)
        flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie"]
        subprocess.run([cc, "-std=c99", *flags, "-c", str(CJSON / "cJSON.c"),
                        "-o", str(temp / "cjson.o")], check=True)
        includes = [CJSON, ROOT / "components/factory/include", ROOT / "components/control/include",
                    ROOT / "components/certification/include"]
        executable = temp / "harness"
        subprocess.run([cxx, "-std=c++17", *flags, "-Wall", "-Wextra", "-Werror", "-no-pie",
                        *[f"-I{p}" for p in includes], str(temp / "harness.cpp"), str(temp / "cjson.o"),
                        "-o", str(executable)], check=True)
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        cases = args.cases or CASES
        failures = []
        for case in cases:
            result = subprocess.run([str(executable), case], env=env, capture_output=True, text=True, timeout=10)
            print(result.stdout, end="")
            if result.returncode:
                failures.append(case)
                print(f"[FAIL] {case}: {result.stderr.strip()}")
        print(f"Factory cancellation API: {len(cases)-len(failures)} passed; failures={failures}")
        return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
