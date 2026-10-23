"""Exercise production backup export/ingest and JSON helpers with real cJSON.

Only hardware, HTTP transport and persistent storage are replaced at the
boundary. Each scenario runs in its own ASan/UBSan process so a memory fault
does not hide unrelated integrity failures.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CJSON = ROOT / "managed_components/espressif__cjson/cJSON"

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>
#include "cJSON.h"
#include "cjson_helpers.hpp"
#include "SignageTypes.hpp"
#include "PowerConfigLimits.hpp"
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101,
              ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
              ESP_ERR_INVALID_SIZE=0x104, ESP_ERR_NOT_FOUND=0x105,
              ESP_ERR_TIMEOUT=0x107, ESP_ERR_NVS_NOT_FOUND=0x1102,
              HTTPD_SOCK_ERR_TIMEOUT=-3;
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t* bytes,size_t length) {
    crc=~crc;
    for(size_t i=0;i<length;++i) {
        crc^=bytes[i];
        for(unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    return ~crc;
}
struct httpd_req_t {
    void* user_ctx=nullptr;
    std::string input,output,status;
    size_t content_len=0,offset=0;
    int writes=0,fail_write=-1;
    bool authorized=true;
};
int httpd_req_recv(httpd_req_t* r,char* out,size_t size) {
    size=std::min({size,r->input.size()-r->offset,size_t{113}});
    std::memcpy(out,r->input.data()+r->offset,size); r->offset+=size;
    return static_cast<int>(size);
}
int httpd_resp_set_type(httpd_req_t*,const char*) { return ESP_OK; }
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*) { return ESP_OK; }
int httpd_resp_send_chunk(httpd_req_t* r,const char* text,size_t size) {
    if(r->writes++==r->fail_write) return ESP_FAIL;
    if(text) r->output.append(text,size);
    return ESP_OK;
}
namespace lg {
constexpr unsigned kPartitionLayoutVersion=2;
struct Storage { unsigned configured_bytes=8*1024*1024; };
Storage capture_storage() { return {}; }
void note_api_allocation_failure() {}
struct MemorySnapshot { uint32_t free_8bit=1024*1024,largest_8bit=1024*1024; };
MemorySnapshot capture_runtime_memory() { return {}; }
constexpr uint32_t kPostTopologyHeapReserveBytes=16*1024;
struct MemoryAdmission { bool accepted=true; };
MemoryAdmission check_memory_admission_snapshot(MemorySnapshot,uint32_t,uint32_t,uint32_t) { return {}; }
__JSON_BUDGET__
__JSON_PARSER__
namespace api {
enum class ApiRole { Admin };
bool authorize(httpd_req_t* r,ApiRole) { return r->authorized; }
struct AuditLog { static AuditLog& instance(){ static AuditLog log; return log; } void log(ApiRole,const char*,const char*) {} };
}
class ConfigStore {
public:
    struct ConfigValidationResult { char first_error[128]{}; };
    static int validate_config(const DeviceConfig&,ConfigValidationResult*) { return ESP_OK; }
    DeviceConfig cfg{};
    SceneConfig scene{};
    SceneMotionConfig motion{};
    Sequence sequence{};
    CalendarSchedule calendar{};
    CueList cue{};
    int config_error=ESP_OK,scene_error=ESP_OK;
    ConfigStore() {
        std::strcpy(cfg.timezone,"EET-2EEST,M3.5.0/3,M10.5.0/4");
        std::strcpy(cfg.site_label,"Workshop"); std::strcpy(cfg.group_label,"Front");
        std::strcpy(cfg.power_supplies[0].name,"LED supply");
        cfg.protocol.merge_mode=MergeMode::LTP;
        for(uint8_t i=0;i<16;++i) { sequence.tracks[i].channel_id=i; sequence.tracks[i].num_steps=0; }
        std::strcpy(scene.name,"Scene");
        scene.channels[0].layers[0].palette_id=13;
        scene.channels[0].layers[0].effect=23;
        motion.track_count=1; motion.tracks[0].target=AutomationTarget::LayerSpeed;
        motion.tracks[0].point_count=2;
        motion.tracks[0].points[0]={0,1000}; motion.tracks[0].points[1]={65535,60000};
    }
    int load_config(DeviceConfig& out,bool=true){ if(config_error==ESP_OK) out=cfg; return config_error; }
    int load_scene(uint8_t i,SceneConfig& out,bool=true){ if(i!=0) return ESP_ERR_NVS_NOT_FOUND; out=scene; return scene_error; }
    int load_scene_motion(uint8_t i,SceneMotionConfig& out){ if(i!=0) return ESP_ERR_NVS_NOT_FOUND; out=motion; return ESP_OK; }
    int load_sequence(Sequence& out,bool* =nullptr,bool=true){ out=sequence; return ESP_OK; }
    int load_calendar_schedule(CalendarSchedule& out){ out=calendar; return ESP_OK; }
    int load_cue_list(uint8_t,CueList& out){ out=cue; return ESP_ERR_NVS_NOT_FOUND; }
    bool scene_exists(uint8_t i){ return i==0; }
};
struct Identity { char id[32]="node",model[32]="lg-led-v1",fw_version[32]="1.0.0-rc1",hw_revision[16]="v1.0"; };
class FactoryApi { public:
    ConfigStore store; Identity identity_;
    ConfigStore* config(){ return &store; }
    const Identity& identity(){ return identity_; }
};
int send_error(httpd_req_t* r,const char* status,const char* code){ r->status=status; r->output=code; return ESP_OK; }
__CONFIG_PARSER__
__SCENE_PARSERS__
__SERIALIZERS__
__EXPORT__
__INGEST__
}

size_t live_allocations=0;
int allocations_left=-1;
void* tracked_malloc(size_t bytes){
    if(allocations_left==0) return nullptr;
    if(allocations_left>0) --allocations_left;
    void* p=std::malloc(bytes); if(p) ++live_allocations; return p;
}
void tracked_free(void* p){ if(p) { assert(live_allocations); --live_allocations; std::free(p); } }
std::string encode(cJSON* j){ assert(j); char* p=cJSON_PrintUnformatted(j); assert(p); std::string s=p; cJSON_free(p); cJSON_Delete(j); return s; }
std::string package(const std::vector<std::string>& records,uint32_t checksum_delta=0,int count_delta=0) {
    std::string data;
    for(const auto& record:records) data+=record+"\n";
    uint32_t crc=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(data.data()),data.size());
    return data+"{\"type\":\"end\",\"records\":"+std::to_string(records.size()+1+count_delta)+
           ",\"checksum\":"+std::to_string(crc^checksum_delta)+"}\n";
}
std::string header="{\"type\":\"header\",\"schema\":4,\"source\":{\"family\":\"led-node\"}}";
std::string scene="{\"type\":\"scene\",\"index\":0,\"data\":{\"globalBrightness\":42}}";
int ingest(const std::string& bytes,lg::ParsedBackupPackage& result) {
    httpd_req_t request; request.input=bytes; request.content_len=bytes.size();
    lg::ConfigStore store;
    return lg::ingest_backup_package(&request,result,&store);
}
int main(int argc,char** argv) {
    using namespace lg;
    assert(argc==2); const std::string name=argv[1];
    cJSON_Hooks hooks{tracked_malloc,tracked_free}; cJSON_InitHooks(&hooks);
    if(name=="short_string") {
        cJSON* j=cJSON_Parse("{\"name\":\"A\"}"); char target[32];
        std::memset(target,0x5a,sizeof(target));
        assert(json::get_string(j,"name",target,sizeof(target)));
        assert(std::strcmp(target,"A")==0);
        for(size_t i=2;i<sizeof(target);++i) assert(target[i]==0);
        cJSON_Delete(j);
    } else if(name=="unsigned_checksum") {
        cJSON* j=cJSON_Parse("{\"checksum\":4294967295}"); uint32_t crc=0;
        assert(json::get_number(j,"checksum",crc) && crc==UINT32_MAX); cJSON_Delete(j);
    } else if(name=="numeric_bool") {
        cJSON* j=cJSON_Parse("{\"enabled\":1}"); bool value=false;
        assert(json::get_bool(j,"enabled",value) && value); cJSON_Delete(j);
    } else if(name=="integer_bounds") {
        for(const char* text:{"{\"value\":-1}","{\"value\":256}","{\"value\":2.5}","{\"value\":1e400}"}) {
            cJSON* j=cJSON_Parse(text); assert(j); uint8_t value=17;
            assert(!json::get_number(j,"value",value) && value==17); cJSON_Delete(j);
        }
    } else if(name=="strict_json") {
        for(const std::string& text:std::vector<std::string>{"{} garbage","{} {}",std::string("{}\0tail",7)}) {
            cJSON* j=parse_admitted_json(text.data(),text.size()); assert(!j);
        }
        cJSON* j=parse_admitted_json("{} \t\r\n",6); assert(j); cJSON_Delete(j);
    } else if(name=="scene_roundtrip") {
        ConfigStore store; std::memset(store.scene.name,'A',23); store.scene.name[23]=0;
        cJSON* j=serialize_scene(store.scene,0); SceneConfig restored{}; char error[128]{};
        assert(parse_scene_record(j,restored,error,sizeof(error)));
        assert(restored.channels[0].layers[0].palette_id==13);
        assert(restored.channels[0].layers[0].effect==23); cJSON_Delete(j);
    } else if(name=="reset_scene_roundtrip") {
        SceneConfig original{}; original.reset();
        cJSON* j=serialize_scene(original,0); SceneConfig restored{}; char error[128]{};
        assert(parse_scene_record(j,restored,error,sizeof(error)));
        assert(restored.channels[15].layers[2].speed_ms==0);
        assert(restored.channels[15].layers[2].opacity==0);
        assert(!restored.channels[15].layers[2].enabled); cJSON_Delete(j);
    } else if(name=="config_numeric_validation") {
        for(const char* payload:{"{\"maxPowerMw\":0.5}","{\"activePreset\":0.5}",
             "{\"syncToken\":2.5}","{\"channels\":[{\"maxBrightness\":1.5}]}",
             "{\"powerSupplies\":[{\"id\":0,\"enabled\":false,\"voltageMv\":-1}]}",
             "{\"powerSupplies\":[{\"id\":0,\"enabled\":false,\"ratedCurrentMa\":1e400}]}",
             "{\"powerSupplies\":[{\"id\":0,\"enabled\":false,\"usableCapacityPermille\":65536}]}",
             "{\"powerSupplies\":[{\"id\":0,\"enabled\":\"false\"}]}"}) {
            cJSON* j=cJSON_Parse(payload); assert(j); DeviceConfig original{},restored{}; char error[128]{};
            assert(!parse_and_validate_device_config(j,original,restored,error,sizeof(error))); cJSON_Delete(j);
        }
    } else if(name=="old_merge_mode") {
        cJSON* j=cJSON_Parse("{\"protocol\":{\"mergeMode\":1},\"gamma\":2.2}");
        DeviceConfig original{},restored{}; char error[128]{};
        assert(parse_and_validate_device_config(j,original,restored,error,sizeof(error)));
        assert(restored.protocol.merge_mode==static_cast<MergeMode>(1) && restored.gamma_x10==22);
        cJSON_Delete(j);
    } else if(name=="valid_legacy") {
        auto result=std::make_unique<ParsedBackupPackage>();
        const std::string bytes="{\n \"version\":3,\n \"presets\":[{\"index\":0,\"globalBrightness\":44}]\n}";
        assert(ingest(bytes,*result)==ESP_OK && result->schema==3 && result->has_scenes[0]);
        assert(result->scenes[0].global_brightness==44);
    } else if(name=="invalid_domain_records") {
        for(const char* record:{
            "{\"type\":\"scene\",\"index\":0,\"data\":{\"transitionMs\":-1}}",
            "{\"type\":\"scene\",\"index\":0,\"data\":{\"name\":\"abcdefghijklmnopqrstuvwxyz\"}}",
            "{\"type\":\"scene\",\"index\":0,\"data\":{\"channels\":[{\"layers\":[{\"paletteId\":99}]}]}}",
            "{\"type\":\"scene\",\"index\":0,\"data\":{\"globalBrightness\":1.5}}",
            "{\"type\":\"sceneMotion\",\"index\":0,\"data\":{\"tracks\":[{\"target\":\"layer_speed\",\"points\":[{\"position\":100,\"value\":100},{\"position\":1,\"value\":100}]}]}}",
            "{\"type\":\"calendar\",\"data\":{\"entries\":[{\"hour\":24}]}}",
            "{\"type\":\"cueList\",\"index\":0,\"data\":{\"cues\":[{\"sceneIndex\":256}]}}"}) {
            auto result=std::make_unique<ParsedBackupPackage>();
            assert(ingest(package({header,record}),*result)!=ESP_OK && result->error[0]);
        }
    } else if(name=="timeline_roundtrip") {
        for(uint16_t bpm:{10,500}) {
            ConfigStore store; store.sequence.bpm=bpm;
            store.sequence.tracks[15].enabled=true; store.sequence.tracks[15].num_steps=1;
            store.sequence.tracks[15].steps[0]={0,64};
            cJSON* j=serialize_timeline(store.sequence); Sequence restored{}; char error[128]{};
            assert(parse_timeline_record(j,restored,error,sizeof(error)));
            assert(restored.bpm==bpm && restored.tracks[15].channel_id==15);
            assert(restored.tracks[15].num_steps==1 && restored.tracks[15].steps[0].length_beats==64);
            cJSON_Delete(j);
        }
    } else if(name=="timeline_missing_scene") {
        auto result=std::make_unique<ParsedBackupPackage>();
        const std::string record="{\"type\":\"timeline\",\"data\":{\"enabled\":false,\"tracks\":[{\"channel_id\":0,\"enabled\":true,\"steps\":[{\"preset_index\":31,\"length_beats\":1}]}]}}";
        assert(ingest(package({header,record}),*result)!=ESP_OK && result->error[0]);
    } else if(name=="timeline_mapping") {
        cJSON* j=cJSON_Parse("{\"bpm\":120}"); Sequence restored{}; char error[128]{};
        assert(parse_timeline_record(j,restored,error,sizeof(error))); cJSON_Delete(j);
        for(uint8_t i=0;i<16;++i) assert(restored.tracks[i].channel_id==i && restored.tracks[i].num_steps==0);
        j=cJSON_Parse("{\"tracks\":[{\"channel_id\":0},{\"channel_id\":0}]}");
        assert(!parse_timeline_record(j,restored,error,sizeof(error))); cJSON_Delete(j);
    } else if(name=="motion_roundtrip") {
        ConfigStore store; cJSON* j=serialize_scene_motion(store.motion,0);
        SceneMotionConfig restored{}; char error[128]{};
        assert(parse_scene_motion_record(j,restored,error,sizeof(error)));
        assert(restored.tracks[0].points[1].value==60000); cJSON_Delete(j);
    } else if(name=="config_roundtrip") {
        ConfigStore store; cJSON* j=serialize_device_config(store.cfg); DeviceConfig initial{},restored{}; char error[128]{};
        assert(parse_and_validate_device_config(j,initial,restored,error,sizeof(error)));
        assert(restored.protocol.merge_mode==MergeMode::LTP);
        assert(std::strcmp(restored.timezone,store.cfg.timezone)==0);
        assert(std::strcmp(restored.site_label,store.cfg.site_label)==0);
        assert(std::strcmp(restored.group_label,store.cfg.group_label)==0);
        assert(std::strcmp(restored.power_supplies[0].name,store.cfg.power_supplies[0].name)==0);
        cJSON_Delete(j);
    } else if(name=="export_roundtrip") {
        FactoryApi api; httpd_req_t request; request.user_ctx=&api;
        assert(get_backup(&request)==ESP_OK);
        auto result=std::make_unique<ParsedBackupPackage>();
        assert(ingest(request.output,*result)==ESP_OK);
        assert(result->has_device_config && result->has_scenes[0] && result->has_motions[0]);
        assert(result->scenes[0].channels[0].layers[0].palette_id==13);
        assert(result->motions[0].tracks[0].points[1].value==60000);
    } else if(name=="export_load_failure") {
        FactoryApi api; api.store.config_error=ESP_FAIL; httpd_req_t request; request.user_ctx=&api;
        const int err=get_backup(&request);
        assert(err!=ESP_OK || request.status=="500 Internal Server Error");
        assert(request.output.find("\"type\":\"end\"")==std::string::npos);
    } else if(name=="export_disconnect") {
        FactoryApi api; httpd_req_t request; request.user_ctx=&api; request.fail_write=2;
        assert(get_backup(&request)!=ESP_OK);
        assert(request.output.find("\"type\":\"end\"")==std::string::npos);
    } else if(name=="export_oom") {
        for(int left:{0,1,8,30,60,100,200,800,1600}) {
            FactoryApi api; httpd_req_t request; request.user_ctx=&api;
            allocations_left=left; const int err=get_backup(&request); allocations_left=-1;
            assert(err!=ESP_OK || request.status=="500 Internal Server Error");
            assert(request.output.find("\"type\":\"end\"")==std::string::npos);
            assert(live_allocations==0);
        }
    } else {
        auto result=std::make_unique<ParsedBackupPackage>();
        if(name=="valid_integrity") assert(ingest(package({header,scene}),*result)==ESP_OK && result->scene_count==1);
        else {
            std::string bytes;
            if(name=="missing_end") bytes=header+"\n"+scene+"\n";
            else if(name=="bad_checksum") bytes=package({header,scene},1);
            else if(name=="bad_count") bytes=package({header,scene},0,1);
            else if(name=="duplicate_scene") bytes=package({header,scene,scene});
            else if(name=="unknown_record") bytes=package({header,"{\"type\":\"unexpected\"}"});
            else if(name=="duplicate_header") bytes=package({header,header,scene});
            else if(name=="missing_header") bytes=package({scene});
            else if(name=="bad_schema") { std::string h=header; h[h.find("\"schema\":4")+9]='5'; bytes=package({h,scene}); }
            else if(name=="after_end") bytes=package({header,scene})+scene+"\n";
            else if(name=="invalid_scene_index") bytes=package({header,"{\"type\":\"scene\",\"index\":99,\"data\":{}}"});
            else if(name=="invalid_legacy") bytes="{\"version\":3,\"config\":{\"maxPowerMw\":-1}}";
            else if(name=="empty_package") bytes=package({header});
            else { std::fprintf(stderr,"Unknown scenario %s\n",name.c_str()); return 2; }
            assert(ingest(bytes,*result)!=ESP_OK && result->error[0]);
        }
    }
    assert(live_allocations==0); cJSON_InitHooks(nullptr);
    std::printf("PASS %s\n",name.c_str());
}
'''

CASES = [
    "short_string", "unsigned_checksum", "numeric_bool", "integer_bounds",
    "strict_json", "scene_roundtrip", "motion_roundtrip", "config_roundtrip",
    "reset_scene_roundtrip", "config_numeric_validation", "old_merge_mode",
    "valid_legacy", "invalid_domain_records",
    "timeline_roundtrip", "timeline_missing_scene", "timeline_mapping",
    "export_roundtrip", "export_load_failure", "export_disconnect", "export_oom",
    "valid_integrity", "missing_end", "bad_checksum", "bad_count",
    "duplicate_scene", "unknown_record", "duplicate_header", "missing_header",
    "bad_schema", "after_end", "invalid_scene_index", "invalid_legacy", "empty_package",
]


def main():
    source = (ROOT / "components/factory/FactoryApi.cpp").read_text()
    memory = (ROOT / "components/factory/include/RuntimeMemory.hpp").read_text()
    sections = {
        "JSON_BUDGET": memory[memory.index("struct JsonScratchBudget"):
                              memory.index("inline MemoryAdmissionResult check_memory_admission_snapshot")],
        "JSON_PARSER": source[source.index("static cJSON* parse_admitted_json("):
                              source.index("esp_err_t read_json(")],
        "CONFIG_PARSER": source[source.index("bool parse_and_validate_device_config("):
                                source.index("esp_err_t post_config_validate(")],
        "SCENE_PARSERS": source[source.index("static bool parse_and_validate_layer_obj("):
                               source.index("esp_err_t put_preset(")],
        "SERIALIZERS": source[source.index("class BackupJsonBuilder"):
                              source.index("// ── Streaming Backup Export")],
        "EXPORT": source[source.index("static bool backup_record_missing("):
                         source.index("// ── Restore Parsed Package Model")],
        "INGEST": source[source.index("struct ParsedBackupPackage"):
                         source.index("// ── Restore Preflight Validation")],
    }
    harness = HARNESS
    for key, value in sections.items():
        harness = harness.replace(f"__{key}__", value)
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cxx or not cc:
        raise SystemExit("Host C and C++ compilers are required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-backup-recovery-") as directory:
        temp = Path(directory)
        (temp / "driver").mkdir()
        (temp / "driver/gpio.h").write_text("#pragma once\ntypedef enum {" +
            ",".join(f"GPIO_NUM_{n}={n}" for n in range(40)) + "} gpio_num_t;\n")
        (temp / "test.cpp").write_text(harness)
        flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie"]
        subprocess.run([cc, "-std=c99", *flags, "-c", str(CJSON / "cJSON.c"), "-o", str(temp / "cjson.o")], check=True)
        includes = [temp, CJSON, ROOT / "components/config/include", ROOT / "components/board/include",
                    ROOT / "components/factory/include"]
        subprocess.run([cxx, "-std=c++17", *flags, "-no-pie", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
                        *[f"-I{p}" for p in includes], str(temp / "test.cpp"), str(temp / "cjson.o"),
                        "-o", str(temp / "test")], check=True)
        env = dict(os.environ)
        env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        failures = []
        for case in sys.argv[1:] or CASES:
            result = subprocess.run([str(temp / "test"), case], env=env)
            if result.returncode:
                failures.append(case)
        print(f"Backup recovery: {len(sys.argv[1:] or CASES)-len(failures)} passed; failures={failures}")
        return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
