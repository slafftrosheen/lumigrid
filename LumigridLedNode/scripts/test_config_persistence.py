"""Exercise the production ConfigStore with bounded NVS error injection.

The fake models API failures and immediately visible writes, as in the pinned
IDF implementation. Physical flash interruption and fragmentation need board
testing; these checks do not claim power-loss atomicity across multiple keys.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SUPPORT = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
    ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
    ESP_ERR_INVALID_SIZE = 0x104, ESP_ERR_NOT_FOUND = 0x105,
    ESP_ERR_INVALID_VERSION = 0x10a,
    ESP_ERR_NVS_NOT_FOUND = 0x1102, ESP_ERR_NVS_INVALID_LENGTH = 0x110c,
    ESP_ERR_NVS_INVALID_STATE = 0x110b, ESP_ERR_NVS_NOT_ENOUGH_SPACE = 0x1105;
inline const char* esp_err_to_name(int) { return "injected"; }
template<class... Args> inline void fake_log(Args&&...) {}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
#define ESP_RETURN_ON_ERROR(expr, ...) do { const esp_err_t e = (expr); if (e != ESP_OK) return e; } while (0)
using nvs_handle_t = uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
inline std::map<std::string, std::vector<uint8_t>> blobs;
inline std::map<std::string, int> read_errors, write_errors, erase_errors;
inline std::string corrupt_write_key;
inline int commit_error = ESP_OK, fail_commit_call = 0, writes = 0, commits = 0, erases = 0, closes = 0;
inline int nvs_open(const char*, nvs_open_mode_t, nvs_handle_t* h) { *h = 1; return ESP_OK; }
inline void nvs_close(nvs_handle_t) { ++closes; }
inline int nvs_get_blob(nvs_handle_t, const char* key, void* data, size_t* size) {
    if (read_errors.count(key)) return read_errors.at(key);
    const auto found = blobs.find(key);
    if (found == blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!data) { *size = found->second.size(); return ESP_OK; }
    if (*size < found->second.size()) { *size = found->second.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = found->second.size();
    if (*size) std::memcpy(data, found->second.data(), *size);
    return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t, const char* key, const void* data, size_t size) {
    ++writes;
    if (write_errors.count(key)) return write_errors.at(key);
    const auto* bytes = static_cast<const uint8_t*>(data);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + size);
    if (corrupt_write_key == key) blobs[key].back() ^= 0x80;
    return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) { ++commits; return commits == fail_commit_call ? ESP_FAIL : commit_error; }
inline int nvs_erase_key(nvs_handle_t, const char* key) {
    ++erases;
    if (erase_errors.count(key)) return erase_errors.at(key);
    return blobs.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
inline int nvs_set_u8(nvs_handle_t h, const char* k, uint8_t v) { return nvs_set_blob(h,k,&v,sizeof(v)); }
inline int nvs_set_u32(nvs_handle_t h, const char* k, uint32_t v) { return nvs_set_blob(h,k,&v,sizeof(v)); }
inline int nvs_get_u8(nvs_handle_t h, const char* k, uint8_t* v) { size_t n=sizeof(*v); return nvs_get_blob(h,k,v,&n); }
inline int nvs_get_u32(nvs_handle_t h, const char* k, uint32_t* v) { size_t n=sizeof(*v); return nvs_get_blob(h,k,v,&n); }
inline uint32_t esp_rom_crc32_le(uint32_t seed, const uint8_t* bytes, size_t size) {
    uint32_t crc = ~seed;
    while (size--) { crc ^= *bytes++; for (int b=0;b<8;++b) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u))); }
    return ~crc;
}
inline void esp_fill_random(void* data, size_t size) { std::memset(data, 19, size); }
namespace lg { struct NvsRuntime { static int initialise() { return ESP_OK; } }; }
'''

HARNESS = r'''
#include "support.hpp"
#include "ConfigStore.cpp"
#include <cassert>
#include <cstdio>
#include <functional>
using namespace lg;
template<class T> void put(const char* key, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + sizeof(value));
}
SceneConfig scene(const char* name="Previous") {
    SceneConfig s{}; std::strncpy(s.name,name,sizeof(s.name)-1); s.checksum=calc_scene_crc(s); return s;
}
DeviceConfig device(const char* name="Previous") {
    DeviceConfig c{}; std::strncpy(c.device_name,name,sizeof(c.device_name)-1); c.checksum=calc_device_crc(c); return c;
}
NetworkSettings network() {
    NetworkSettings s{}; s.mode=NetworkMode::StaWithApFallback;
    std::strcpy(s.sta_ssid,"fixture-network"); std::strcpy(s.sta_password,"old-password");
    std::strcpy(s.ap_password,"old-ap-password"); s.checksum=calc_network_settings_crc(s); return s;
}
Sequence sequence() {
    Sequence s{}; for (uint8_t i=0;i<16;++i) s.tracks[i].channel_id=i;
    s.bpm=143; s.checksum=calc_seq_crc(s); return s;
}
CalendarSchedule calendar() {
    CalendarSchedule c{}; c.entries[0].hour=19; c.entries[0].enabled=true;
    c.checksum=calc_calendar_crc(c); return c;
}
void reset_calls() { writes=commits=erases=closes=0; }

int main(int argc, char** argv) {
    assert(argc==2); const std::string test=argv[1];
    ConfigStore store;
    put("scene_0",scene());
    assert(store.init()==ESP_OK);
    reset_calls();
    if (test=="network_roundtrip") {
        auto input=network(); assert(store.save_network_settings(input)==ESP_OK);
        NetworkSettings loaded{}; assert(store.load_network_settings(loaded)==ESP_OK);
        assert(loaded.schema_version==2 && std::strcmp(loaded.ap_password,input.ap_password)==0);
        assert(std::strcmp(loaded.sta_ssid,input.sta_ssid)==0);
    } else if (test=="network_mislabeled_v2") {
        auto input=network(); input.schema_version=1; input.checksum=calc_network_settings_crc(input); put("network",input);
        NetworkSettings loaded{}; assert(store.load_network_settings(loaded)==ESP_OK);
        assert(loaded.schema_version==2 && std::strcmp(loaded.ap_password,input.ap_password)==0);
    } else if (test=="network_read_failure") {
        read_errors["network"]=ESP_FAIL;
        NetworkSettings loaded{}; assert(store.load_network_settings(loaded)==ESP_FAIL);
        assert(writes==0 && commits==0);
    } else if (test=="network_migration_write_failure") {
        NetworkSettingsV1 old{}; old.checksum=calc_network_settings_v1_crc(old); put("network",old);
        write_errors["network"]=ESP_FAIL;
        NetworkSettings loaded{}; assert(store.load_network_settings(loaded)==ESP_FAIL);
        assert(blobs.at("network").size()==sizeof(old));
    } else if (test=="network_backup_write_failure") {
        auto old=network(); put("network",old); put("network_b",old); const auto backup=blobs.at("network_b");
        write_errors["network_b"]=ESP_FAIL;
        assert(store.save_network_settings(old)==ESP_FAIL); assert(blobs.at("network_b")==backup);
    } else if (test=="scene_missing_primary" || test=="scene_wrong_length" || test=="scene_empty_primary") {
        blobs.erase("scene_0"); put("scene_b_0",scene());
        if(test=="scene_wrong_length") blobs["scene_0"]={0,1,2};
        if(test=="scene_empty_primary") blobs["scene_0"]={};
        SceneConfig loaded{}; assert(store.load_scene(0,loaded)==ESP_OK);
        assert(std::strcmp(loaded.name,"Previous")==0); assert(writes==0 && commits==0);
    } else if (test=="sequence_missing_primary") {
        put("seq_main_bak",sequence()); Sequence loaded{};
        assert(store.load_sequence(loaded)==ESP_OK); assert(loaded.bpm==143);
    } else if (test=="calendar_missing_primary" || test=="calendar_wrong_length") {
        put("calendar_bak",calendar()); if(test=="calendar_wrong_length") blobs["calendar"]={};
        CalendarSchedule loaded{}; assert(store.load_calendar_schedule(loaded)==ESP_OK);
        assert(loaded.entries[0].hour==19 && loaded.entries[0].enabled); assert(writes==0);
    } else if (test=="config_empty_primary") {
        blobs["device_cfg"]={}; put("device_cfg_bak",device()); DeviceConfig loaded{};
        assert(store.load_config(loaded)==ESP_OK); assert(std::strcmp(loaded.device_name,"Previous")==0);
    } else if (test=="config_invalid_semantics") {
        auto bad=device("Unsafe"); bad.channels[0].enabled=true; bad.channels[0].pixel_count=65535;
        bad.checksum=calc_device_crc(bad); put("device_cfg",bad); put("device_cfg_bak",device());
        DeviceConfig loaded{}; assert(store.load_config(loaded)==ESP_OK);
        assert(std::strcmp(loaded.device_name,"Previous")==0 && !loaded.channels[0].enabled);
    } else if (test=="scene_backup_write_failure" || test=="scene_backup_commit_failure") {
        put("scene_b_0",scene()); const auto backup=blobs.at("scene_b_0");
        if(test=="scene_backup_write_failure") write_errors["scene_b_0"]=ESP_FAIL; else fail_commit_call=2;
        assert(store.save_scene(0,scene("Changed"))==ESP_FAIL);
        if(test=="scene_backup_write_failure") assert(blobs.at("scene_b_0")==backup);
    } else if (test=="scene_verify_before_backup") {
        put("scene_b_0",scene()); const auto backup=blobs.at("scene_b_0"); corrupt_write_key="scene_0";
        assert(store.save_scene(0,scene("Changed"))!=ESP_OK); assert(blobs.at("scene_b_0")==backup);
        corrupt_write_key.clear(); SceneConfig loaded{}; assert(store.load_scene(0,loaded)==ESP_OK);
        assert(std::strcmp(loaded.name,"Previous")==0);
    } else if (test=="delete_backup_error") {
        put("scene_b_0",scene()); erase_errors["scene_b_0"]=ESP_FAIL;
        assert(store.delete_scene(0)==ESP_FAIL); assert(blobs.count("scene_0"));
    } else if (test=="delete_backup_only_commit") {
        blobs.erase("scene_0"); put("scene_b_0",scene()); commit_error=ESP_FAIL;
        assert(store.delete_scene(0)==ESP_FAIL);
    } else if (test=="marker_checksum_failure" || test=="marker_commit_failure") {
        if(test=="marker_checksum_failure") write_errors["rst_crc"]=ESP_FAIL; else commit_error=ESP_FAIL;
        assert(store.set_restore_in_progress(true,0x1234)==ESP_FAIL);
    } else if (test=="defaults_preserve_nonzero_scene") {
        blobs.clear(); put("scene_1",scene("Keep me")); const auto before=blobs;
        ConfigStore fresh; assert(fresh.init()==ESP_OK); assert(blobs==before);
    } else if (test=="cue_bounds") {
        CueList list{}; list.count=kMaxCuesPerList+1; list.checksum=calc_cue_list_crc(list); put("cuel_0",list);
        CueList loaded{}; assert(store.load_cue_list(0,loaded)!=ESP_OK); assert(writes==0);
        assert(store.save_cue_list(0,list)==ESP_ERR_INVALID_ARG);
    } else if (test=="motion_bounds") {
        SceneMotionConfig motion{}; motion.track_count=kAutomationTracksPerScene+1;
        motion.checksum=calc_motion_crc(motion); put("smot_0",motion);
        SceneMotionConfig loaded{}; assert(store.load_scene_motion(0,loaded)!=ESP_OK); assert(writes==0);
        assert(store.save_scene_motion(0,motion)==ESP_ERR_INVALID_ARG);
    } else if (test=="scene_unterminated_name") {
        auto bad=scene(); std::memset(bad.name,'X',sizeof(bad.name)); bad.checksum=calc_scene_crc(bad); put("scene_0",bad);
        SceneConfig loaded{}; assert(store.load_scene(0,loaded)!=ESP_OK); assert(writes==0);
        assert(store.save_scene(0,bad)==ESP_ERR_INVALID_ARG);
    } else if (test=="all_mirror_write_errors" || test=="all_mirror_commit_errors" || test=="all_mirror_verify_errors") {
        SceneMotionConfig motion{}; motion.checksum=calc_motion_crc(motion); put("smot_0",motion); put("smot_b_0",motion);
        CueList cues{}; cues.checksum=calc_cue_list_crc(cues); put("cuel_0",cues); put("cuel_b_0",cues);
        put("scene_b_0",scene()); put("seq_main",sequence()); put("seq_main_bak",sequence());
        put("calendar",calendar()); put("calendar_bak",calendar());
        put("network",network()); put("network_b",network()); put("device_cfg",device()); put("device_cfg_bak",device());
        struct Mirror { const char* primary; const char* backup; std::function<int()> save; };
        const std::vector<Mirror> mirrors{
            {"scene_0","scene_b_0",[&]{return store.save_scene(0,scene("Changed"));}},
            {"smot_0","smot_b_0",[&]{return store.save_scene_motion(0,motion);}},
            {"cuel_0","cuel_b_0",[&]{return store.save_cue_list(0,cues);}},
            {"seq_main","seq_main_bak",[&]{return store.save_sequence(sequence());}},
            {"calendar","calendar_bak",[&]{return store.save_calendar_schedule(calendar());}},
            {"network","network_b",[&]{return store.save_network_settings(network());}},
            {"device_cfg","device_cfg_bak",[&]{return store.save_config(device("Changed"));}},
        };
        const auto initial=blobs;
        for (const auto& mirror : mirrors) {
            for(int phase=1;phase<=2;++phase) {
                blobs=initial; reset_calls(); write_errors.clear(); corrupt_write_key.clear(); fail_commit_call=0;
                const auto backup=blobs.at(mirror.backup);
                if(test=="all_mirror_write_errors") write_errors[phase==1 ? mirror.primary : mirror.backup]=ESP_FAIL;
                if(test=="all_mirror_commit_errors") fail_commit_call=phase;
                if(test=="all_mirror_verify_errors") corrupt_write_key=mirror.primary;
                assert(mirror.save()!=ESP_OK);
                if(test!="all_mirror_commit_errors" || phase==1) assert(blobs.at(mirror.backup)==backup);
            }
        }
    } else if (test=="motion_cue_roundtrip") {
        SceneMotionConfig motion{}; motion.track_count=1; motion.tracks[0].point_count=2;
        motion.tracks[0].points[1].position=65535; motion.tracks[0].points[1].value=60000;
        assert(store.save_scene_motion(0,motion)==ESP_OK);
        SceneMotionConfig loaded{}; assert(store.load_scene_motion(0,loaded)==ESP_OK);
        assert(loaded.tracks[0].points[1].value==60000);
        CueList cue{}; cue.count=1; cue.cues[0].scene_index=31; std::strcpy(cue.cues[0].label,"Saved cue");
        assert(store.save_cue_list(0,cue)==ESP_OK); CueList read{};
        assert(store.load_cue_list(0,read)==ESP_OK && read.cues[0].scene_index==31);
        assert(std::strcmp(read.cues[0].label,"Saved cue")==0);
    } else if (test=="motion_cue_delete_backup_only") {
        SceneMotionConfig motion{}; motion.checksum=calc_motion_crc(motion); put("smot_b_0",motion);
        commit_error=ESP_FAIL; assert(store.delete_scene_motion(0)==ESP_FAIL);
        CueList list{}; list.checksum=calc_cue_list_crc(list); put("cuel_b_0",list);
        assert(store.delete_cue_list(0)==ESP_FAIL);
    } else if (test=="marker_read_failure") {
        read_errors["rst_prog"]=ESP_FAIL; bool active=false; uint32_t checksum=99;
        assert(store.read_restore_marker(active,&checksum)==ESP_FAIL && active && checksum==0);
        assert(store.is_restore_in_progress()); assert(writes==0);
    } else if (test=="marker_missing_checksum") {
        put("rst_prog",uint8_t{1}); bool active=false;
        assert(store.read_restore_marker(active)!=ESP_OK && active);
        assert(store.is_restore_in_progress());
    } else if (test=="marker_clear_failures") {
        for (int phase=0;phase<4;++phase) {
            write_errors.clear(); erase_errors.clear(); fail_commit_call=0; reset_calls();
            put("rst_prog",uint8_t{1}); put("rst_crc",uint32_t{0x12345678});
            if(phase==0) write_errors["rst_prog"]=ESP_FAIL;
            if(phase==1) fail_commit_call=1;
            if(phase==2) erase_errors["rst_crc"]=ESP_FAIL;
            if(phase==3) fail_commit_call=2;
            assert(store.set_restore_in_progress(false)==ESP_FAIL);
            bool active=false; uint32_t checksum=0;
            assert(store.read_restore_marker(active,&checksum)==ESP_OK && active && checksum==0x12345678);
        }
    } else if (test=="marker_roundtrip") {
        assert(store.set_restore_in_progress(true,0x11223344)==ESP_OK);
        bool active=false; uint32_t checksum=0;
        assert(store.read_restore_marker(active,&checksum)==ESP_OK && active && checksum==0x11223344);
        assert(store.set_restore_in_progress(false)==ESP_OK);
        assert(store.read_restore_marker(active,&checksum)==ESP_OK && !active && checksum==0);
    } else if (test=="corrupt_records_do_not_create_defaults") {
        blobs.clear(); blobs["scene_0"]={0,1}; const auto original=blobs;
        ConfigStore fresh; assert(fresh.init()!=ESP_OK); assert(blobs==original && writes==0);
        blobs["device_cfg"]={0,1}; DeviceConfig cfg{};
        assert(store.load_config(cfg)!=ESP_OK && writes==0);
        blobs["calendar"]={0,1}; CalendarSchedule schedule{};
        assert(store.load_calendar_schedule(schedule)!=ESP_OK);
        blobs["seq_main"]={}; Sequence seq{}; assert(store.load_sequence(seq)!=ESP_OK);
    } else if (test=="backup_config_load_is_readonly") {
        put("device_cfg_bak",device()); DeviceConfig cfg{};
        assert(store.load_config(cfg)==ESP_OK && std::strcmp(cfg.device_name,"Previous")==0);
        assert(writes==0 && commits==0 && !store.is_primary_valid() && store.is_backup_valid());
        assert(store.persistence_state()==ConfigPersistenceState::BackupOnly);
    } else if (test=="network_invalid_strings") {
        auto bad=network(); std::memset(bad.ap_password,'x',sizeof(bad.ap_password));
        bad.checksum=calc_network_settings_crc(bad); put("network",bad);
        NetworkSettings loaded{}; assert(store.load_network_settings(loaded)!=ESP_OK);
        assert(store.save_network_settings(bad)==ESP_ERR_INVALID_ARG && writes==0);
    } else if (test=="unsupported_schemas") {
        auto net=network(); net.schema_version=99; net.checksum=calc_network_settings_crc(net);
        put("network",net); put("network_b",net); NetworkSettings output{};
        assert(store.load_network_settings(output)==ESP_ERR_INVALID_VERSION);
        auto cfg=device(); cfg.schema_version=99; cfg.checksum=calc_device_crc(cfg);
        put("device_cfg",cfg); DeviceConfig loaded{};
        assert(store.load_config(loaded)==ESP_ERR_INVALID_VERSION && writes==0);
    } else if (test=="sequence_calendar_presence_and_delete") {
        Sequence seq{}; CalendarSchedule schedule{}; bool present=true;
        assert(store.load_sequence(seq,&present)==ESP_OK && !present);
        present=true; assert(store.load_calendar_schedule(schedule,&present)==ESP_OK && !present);
        assert(store.delete_sequence()==ESP_OK && store.delete_calendar_schedule()==ESP_OK && commits==0);
        assert(store.save_sequence(sequence())==ESP_OK && store.save_calendar_schedule(calendar())==ESP_OK);
        assert(store.load_sequence(seq,&present)==ESP_OK && present);
        present=false; assert(store.load_calendar_schedule(schedule,&present)==ESP_OK && present);
        assert(store.delete_sequence()==ESP_OK && store.delete_calendar_schedule()==ESP_OK);
        assert(store.load_sequence(seq,&present)==ESP_OK && !present);
        present=true; assert(store.load_calendar_schedule(schedule,&present)==ESP_OK && !present);
        assert(!blobs.count("seq_main") && !blobs.count("seq_main_bak"));
        assert(!blobs.count("calendar") && !blobs.count("calendar_bak"));
    } else if (test=="sequence_calendar_delete_errors") {
        const std::vector<std::pair<std::string,std::function<int()>>> deletes{
            {"seq_main",[&]{return store.delete_sequence();}},
            {"calendar",[&]{return store.delete_calendar_schedule();}},
        };
        for(const auto& operation:deletes) {
            const std::string backup=operation.first+"_bak";
            blobs[operation.first]={1}; blobs[backup]={2};
            erase_errors[backup]=ESP_FAIL;
            assert(operation.second()==ESP_FAIL && blobs.at(operation.first)==std::vector<uint8_t>{1});
            erase_errors.clear(); blobs.erase(operation.first); reset_calls(); commit_error=ESP_FAIL;
            assert(operation.second()==ESP_FAIL && commits==1);
            commit_error=ESP_OK;
        }
    } else if (test=="reset_defaults_roundtrip") {
        SceneConfig expected{}; expected.reset();
        assert(expected.channels[15].layers[2].speed_ms==0 && !expected.channels[15].layers[2].enabled);
        assert(store.save_scene(0,expected)==ESP_OK); SceneConfig loaded{};
        assert(store.load_scene(0,loaded)==ESP_OK);
        expected.checksum=loaded.checksum; assert(std::memcmp(&expected,&loaded,sizeof(expected))==0);
        SceneMotionConfig motion{}; motion.reset(); assert(store.save_scene_motion(0,motion)==ESP_OK);
        SceneMotionConfig read_motion{}; assert(store.load_scene_motion(0,read_motion)==ESP_OK && read_motion.track_count==0);
        CueList cue{}; cue.reset(); assert(store.save_cue_list(0,cue)==ESP_OK);
        CueList read_cue{}; assert(store.load_cue_list(0,read_cue)==ESP_OK && read_cue.count==0);
        DeviceConfig cfg{}; cfg.reset(); assert(store.save_config(cfg)==ESP_OK);
        DeviceConfig read_cfg{}; assert(store.load_config(read_cfg)==ESP_OK);
        assert(std::strcmp(read_cfg.device_name,"lumigrid")==0);
    } else if (test=="readonly_migrations") {
        auto cfg=device(); cfg.schema_version=3; cfg.checksum=calc_device_crc(cfg); put("device_cfg",cfg);
        SceneConfigV1 legacy_scene{}; std::strcpy(legacy_scene.name,"Legacy");
        legacy_scene.checksum=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(&legacy_scene),sizeof(legacy_scene)-sizeof(uint32_t));
        put("scene_0",legacy_scene);
        SequenceV1 legacy_seq{}; for(uint8_t i=0;i<8;++i) legacy_seq.tracks[i].channel_id=i;
        legacy_seq.checksum=calc_seq_v1_crc(legacy_seq); put("seq_main",legacy_seq);
        const auto original=blobs; DeviceConfig loaded_cfg{}; SceneConfig loaded_scene{}; Sequence loaded_seq{}; bool present=false;
        assert(store.load_config(loaded_cfg,false)==ESP_OK && loaded_cfg.schema_version==4);
        assert(store.load_scene(0,loaded_scene,false)==ESP_OK && loaded_scene.schema_version==2);
        assert(store.load_sequence(loaded_seq,&present,false)==ESP_OK && present);
        assert(writes==0 && commits==0 && blobs==original);
    } else if (test=="readonly_missing_config") {
        DeviceConfig cfg{}; const auto original=blobs;
        assert(store.load_config(cfg,false)==ESP_ERR_NVS_NOT_FOUND);
        assert(writes==0 && commits==0 && blobs==original);
    } else if (test=="init_preserves_restore_marker") {
        blobs.clear(); put("rst_prog",uint8_t{1}); put("rst_crc",uint32_t{123});
        auto original=blobs; ConfigStore recovery;
        assert(recovery.init()==ESP_OK && writes==0 && commits==0 && blobs==original);
        put("rst_prog",uint8_t{2}); original=blobs; ConfigStore malformed;
        assert(malformed.init()!=ESP_OK && writes==0 && commits==0 && blobs==original);
    } else if (test=="nested_collection_bounds") {
        SceneMotionConfig motion{}; motion.track_count=1; motion.tracks[0].point_count=kAutomationPointsPerTrack+1;
        assert(store.save_scene_motion(0,motion)==ESP_ERR_INVALID_ARG);
        CueList cue{}; cue.count=1; cue.cues[0].scene_index=kMaxPresets;
        assert(store.save_cue_list(0,cue)==ESP_ERR_INVALID_ARG);
        auto seq=sequence(); seq.tracks[0].num_steps=17; assert(store.save_sequence(seq)==ESP_ERR_INVALID_ARG);
        auto schedule=calendar(); schedule.entries[0].hour=24;
        assert(store.save_calendar_schedule(schedule)==ESP_ERR_INVALID_ARG && writes==0);
    } else { assert(false && "unknown test"); }
    std::printf("[PASS] config persistence: %s\n",test.c_str());
}
'''

CASES = (
    "network_roundtrip", "network_mislabeled_v2", "network_read_failure",
    "network_migration_write_failure", "network_backup_write_failure",
    "scene_missing_primary", "scene_wrong_length", "scene_empty_primary",
    "sequence_missing_primary", "calendar_missing_primary", "calendar_wrong_length",
    "config_empty_primary", "config_invalid_semantics", "scene_backup_write_failure",
    "scene_backup_commit_failure", "scene_verify_before_backup", "delete_backup_error",
    "delete_backup_only_commit", "marker_checksum_failure", "marker_commit_failure",
    "defaults_preserve_nonzero_scene", "cue_bounds", "motion_bounds", "scene_unterminated_name",
    "all_mirror_write_errors", "all_mirror_commit_errors", "all_mirror_verify_errors",
    "motion_cue_roundtrip", "motion_cue_delete_backup_only", "marker_read_failure",
    "marker_missing_checksum", "marker_clear_failures", "marker_roundtrip",
    "corrupt_records_do_not_create_defaults", "backup_config_load_is_readonly",
    "network_invalid_strings", "unsupported_schemas", "nested_collection_bounds",
    "sequence_calendar_presence_and_delete", "sequence_calendar_delete_errors", "reset_defaults_roundtrip",
    "readonly_migrations", "readonly_missing_config", "init_preserves_restore_marker",
)


def main():
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-config-persistence-") as directory:
        temp = Path(directory)
        (temp / "support.hpp").write_text(SUPPORT)
        for name in ("esp_err.h", "esp_check.h", "esp_log.h", "esp_random.h", "esp_rom_crc.h",
                     "nvs.h", "nvs_flash.h", "NvsRuntime.hpp"):
            (temp / name).write_text('#include "support.hpp"\n')
        (temp / "driver").mkdir()
        (temp / "driver/gpio.h").write_text('#pragma once\n#include <cstdint>\nusing gpio_num_t=int;\n' +
            '\n'.join(f'constexpr int GPIO_NUM_{pin}={pin};' for pin in (4,16,17,18,19,21,22,23,25,26,27)))
        (temp / "harness.cpp").write_text(HARNESS)
        executable = temp / "harness"
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-error=unused-parameter",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I", str(temp), "-I", str(ROOT / "components/config"),
                        "-I", str(ROOT / "components/config/include"), "-I", str(ROOT / "components/board/include"),
                        str(temp / "harness.cpp"), "-o", str(executable)], check=True)
        environment = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                       "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        failed = []
        for case in sys.argv[1:] or CASES:
            result = subprocess.run([str(executable), case], env=environment, capture_output=True, text=True)
            print(result.stdout, end="")
            if result.returncode:
                failed.append(case)
                print(f"[FAIL] {case}: {result.stderr.strip()}")
        if failed:
            raise SystemExit(f"{len(failed)} failing persistence scenarios: {', '.join(failed)}")


if __name__ == "__main__":
    main()
