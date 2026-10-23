"""Run the production certification store against an NVS failure-injection fake.

The fake checks API/error semantics; flash wear, capacity and power-loss behavior
still require the real NVS implementation on the board.
"""
from pathlib import Path
import os
import shutil
import subprocess
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
    ESP_ERR_INVALID_CRC = 0x109, ESP_ERR_NVS_NOT_FOUND = 0x1102,
    ESP_ERR_NVS_INVALID_LENGTH = 0x110c, ESP_ERR_NVS_NOT_ENOUGH_SPACE = 0x1105;
inline const char* esp_err_to_name(esp_err_t) { return "injected error"; }
template<class... Args> inline void fake_log(Args&&...) {}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
using nvs_handle_t = uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
inline std::map<std::string, std::vector<uint8_t>> blobs;
inline std::map<std::string, int> read_errors;
inline int open_result = ESP_OK, set_result = ESP_OK, commit_result = ESP_OK,
    erase_result = ESP_OK, erase_key_result = ESP_OK, fail_commit_call = 0,
    set_calls = 0, commit_calls = 0, close_calls = 0;
inline size_t entry_budget = static_cast<size_t>(-1), other_entries = 0;
inline size_t blob_entries(size_t size) { return (size + 31) / 32 + (size + 3999) / 4000 + 1; }
inline int nvs_open(const char*, nvs_open_mode_t mode, nvs_handle_t* handle) {
    if (open_result != ESP_OK) return open_result;
    if (mode == NVS_READONLY && blobs.empty()) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; return ESP_OK;
}
inline void nvs_close(nvs_handle_t) { ++close_calls; }
inline int nvs_get_blob(nvs_handle_t, const char* key, void* output, size_t* size) {
    if (read_errors.count(key)) return read_errors[key];
    auto it = blobs.find(key);
    if (it == blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!output) { *size = it->second.size(); return ESP_OK; }
    if (*size < it->second.size()) { *size = it->second.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = it->second.size();
    std::memcpy(output, it->second.data(), *size); return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t, const char* key, const void* data, size_t size) {
    ++set_calls;
    if (set_result != ESP_OK) return set_result;
    // Logical entry-budget model, including old value while replacement is
    // written. This does not model physical page fragmentation or GC.
    size_t occupied = other_entries + 1 + blob_entries(size);
    for (const auto& item : blobs) occupied += blob_entries(item.second.size());
    if (occupied > entry_budget) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    const auto* bytes = static_cast<const uint8_t*>(data);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + size); return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) {
    ++commit_calls;
    return fail_commit_call == commit_calls ? ESP_FAIL : commit_result;
}
inline int nvs_erase_key(nvs_handle_t, const char* key) {
    if (erase_key_result != ESP_OK) return erase_key_result;
    return blobs.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
inline int nvs_erase_all(nvs_handle_t) {
    if (erase_result != ESP_OK) return erase_result;
    blobs.clear(); return ESP_OK;
}
inline uint32_t esp_rom_crc32_le(uint32_t seed, const uint8_t* data, size_t length) {
    uint32_t crc = ~seed;
    while (length--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
'''

HARNESS = r'''
#include "support.hpp"
#include "CertificationStore.hpp"
#include "CertificationStore.cpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace lg;

CertificationRecord valid_record(uint32_t sequence = 5) {
    CertificationRecord result;
    result.sequence = sequence;
    result.session.state = CertificationState::InProgress;
    result.session.identity.plan_version = kCertificationPlanVersion;
    result.result_count = static_cast<uint16_t>(CertificationPlan::test_count());
    result.session.test_count = result.result_count;
    assert(result.result_count <= kMaxCertificationTests);
    const auto* definitions = CertificationPlan::test_definitions();
    for (size_t i = 0; i < result.result_count; ++i) {
        std::strncpy(result.results[i].test_id, definitions[i].id, sizeof(result.results[i].test_id) - 1);
        result.results[i].evidence_source = definitions[i].required_evidence;
        std::strncpy(result.results[i].unit, definitions[i].unit, sizeof(result.results[i].unit) - 1);
    }
    result.checksum = CertificationStore::calculate_crc(result);
    return result;
}
void put(const char* slot, CertificationRecord record) {
    record.checksum = CertificationStore::calculate_crc(record);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
    blobs[slot] = std::vector<uint8_t>(bytes, bytes + sizeof(record));
}
void verify_invalid(CertificationRecord record) {
    blobs.clear(); put("cert_a", record);
    CertificationStore store;
    CertificationRecord output;
    assert(store.load(output) != ESP_OK);
    assert(store.init() == ESP_OK);
    assert(store.current_record().session.state == CertificationState::NotStarted);
    assert(set_calls == 0 && commit_calls == 0);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string name = argv[1];
    CertificationStore store;
    if (name == "commit_failure" || name == "write_failure") {
        assert(store.init() == ESP_OK);
        const auto before = store.current_record();
        auto record = valid_record();
        (name == "commit_failure" ? commit_result : set_result) = ESP_FAIL;
        assert(store.save(record) == ESP_FAIL);
        assert(std::memcmp(&before, &store.current_record(), sizeof(before)) == 0);
        assert(set_calls == 1);
        assert(commit_calls == (name == "commit_failure" ? 2 : 0));
    } else if (name == "erase_open_failure" || name == "erase_failure" || name == "erase_commit_failure") {
        put("cert_a", valid_record());
        assert(store.init() == ESP_OK);
        const auto before = store.current_record();
        if (name == "erase_open_failure") open_result = ESP_FAIL;
        if (name == "erase_failure") erase_result = ESP_FAIL;
        if (name == "erase_commit_failure") commit_result = ESP_FAIL;
        assert(store.erase() == ESP_FAIL);
        assert(std::memcmp(&before, &store.current_record(), sizeof(before)) == 0);
    } else if (name == "single_alternating_write") {
        assert(store.init() == ESP_OK);
        auto record = valid_record();
        assert(store.save(record) == ESP_OK);
        assert(set_calls == 1 && commit_calls == 1 && blobs.size() == 1);
        const auto old_slot = *blobs.begin();
        assert(store.save(record) == ESP_OK);
        assert(set_calls == 2 && commit_calls == 2 && blobs.size() == 2);
        assert(blobs.at(old_slot.first) == old_slot.second);
    } else if (name == "recovered_slot_preserved" || name == "new_session_sequence") {
        put("cert_a", valid_record(100));
        const auto old = blobs.at("cert_a");
        assert(store.init() == ESP_OK);
        assert(set_calls == 0 && commit_calls == 0);
        auto record = valid_record(0);
        if (name == "new_session_sequence") {
            reset_certification_record(store.current_record());
            store.current_record() = record;
        }
        assert(store.save(name == "new_session_sequence" ? store.current_record() : record) == ESP_OK);
        assert(blobs.at("cert_a") == old);
        assert(store.current_record().sequence == 101);
        CertificationStore reloaded;
        assert(reloaded.init() == ESP_OK && reloaded.current_record().sequence == 101);
    } else if (name == "load_does_not_write") {
        put("cert_b", valid_record());
        CertificationRecord loaded;
        assert(store.load(loaded) == ESP_OK);
        assert(set_calls == 0 && commit_calls == 0 && blobs.size() == 1);
    } else if (name == "read_errors_propagate") {
        put("cert_a", valid_record()); read_errors["cert_b"] = ESP_FAIL;
        assert(store.init() == ESP_FAIL && !store.is_initialized());
        assert(set_calls == 0 && commit_calls == 0);
    } else if (name == "bad_slot_falls_back") {
        put("cert_a", valid_record(7)); put("cert_b", valid_record(8));
        blobs["cert_b"][offsetof(CertificationRecord, checksum)] ^= 0xff;
        assert(store.init() == ESP_OK && store.current_record().sequence == 7);
        assert(set_calls == 0 && commit_calls == 0);
    } else if (name == "sequence_wrap") {
        put("cert_a", valid_record(std::numeric_limits<uint32_t>::max()));
        put("cert_b", valid_record(std::numeric_limits<uint32_t>::max() - 1));
        assert(store.init() == ESP_OK);
        auto record = valid_record();
        assert(store.save(record) == ESP_OK && store.current_record().sequence == 0);
        CertificationStore reloaded;
        assert(reloaded.init() == ESP_OK && reloaded.current_record().sequence == 0);
    } else if (name == "malformed_record_validation") {
        auto record = valid_record(); record.result_count = kMaxCertificationTests + 1; verify_invalid(record);
        record = valid_record(); ++record.session.test_count; verify_invalid(record);
        record = valid_record(); record.session.profile = static_cast<CertificationProfile>(255); verify_invalid(record);
        record = valid_record(); record.session.state = static_cast<CertificationState>(255); verify_invalid(record);
        record = valid_record(); record.results[0].state = static_cast<CertificationTestState>(255); verify_invalid(record);
        record = valid_record(); record.results[0].evidence_source = static_cast<EvidenceSource>(255); verify_invalid(record);
        record = valid_record(); std::memset(record.session.identity.firmware_version, 'X', sizeof(record.session.identity.firmware_version)); verify_invalid(record);
        record = valid_record(); std::memset(record.results[0].note, 'X', sizeof(record.results[0].note)); verify_invalid(record);
        record = valid_record(); record.results[0].measured_value = std::nanf(""); verify_invalid(record);
        record = valid_record(); std::strcpy(record.results[0].test_id, "UNKNOWN"); verify_invalid(record);
        record = valid_record(); std::strcpy(record.results[1].test_id, record.results[0].test_id); verify_invalid(record);
        record = valid_record(); record.schema_version = 1; verify_invalid(record);
        record = valid_record(); record.session.state = CertificationState::Passed; verify_invalid(record);
    } else if (name == "malformed_save_rejected") {
        assert(store.init() == ESP_OK);
        auto record = valid_record(); record.result_count = kMaxCertificationTests + 1;
        assert(store.save(record) == ESP_ERR_INVALID_ARG && set_calls == 0);
    } else if (name == "erase_success") {
        put("cert_a", valid_record()); assert(store.init() == ESP_OK);
        assert(store.erase() == ESP_OK && blobs.empty());
        assert(store.is_initialized() && store.current_record().session.state == CertificationState::NotStarted);
    } else if (name == "inactive_erase_failure" || name == "inactive_commit_failure" ||
               name == "write_after_erase_failure" || name == "final_commit_failure") {
        put("cert_a", valid_record(100)); put("cert_b", valid_record(99));
        const auto active = blobs.at("cert_a");
        assert(store.init() == ESP_OK);
        if (name == "inactive_erase_failure") erase_key_result = ESP_FAIL;
        if (name == "inactive_commit_failure") fail_commit_call = 1;
        if (name == "write_after_erase_failure") set_result = ESP_FAIL;
        if (name == "final_commit_failure") fail_commit_call = 2;
        auto record = valid_record();
        assert(store.save(record) == ESP_FAIL);
        assert(blobs.at("cert_a") == active && store.current_record().sequence == 100);
        CertificationStore reloaded;
        assert(reloaded.init() == ESP_OK && reloaded.current_record().sequence == 100);
    } else if (name == "encoded_capacity_and_roundtrip") {
        entry_budget = 3 * 126; // Four 4 KB pages, with one reserved by NVS.
        other_entries = 130; // Two 1676-byte config blobs plus representative other keys.
        assert(store.init() == ESP_OK);
        auto record = valid_record();
        assert(store.save(record) == ESP_OK);
        const size_t initial = blobs.begin()->second.size();
        record.session.state = CertificationState::Passed;
        record.session.passed_count = record.result_count;
        for (size_t i = 0; i < record.result_count; ++i) {
            auto& result = record.results[i];
            result.state = CertificationTestState::Passed;
            result.started_ms = 1000 + i * 100;
            result.completed_ms = 1050 + i * 100;
            result.measured_value = 50.0f;
            result.attempts = 1;
            std::strcpy(result.note, "Bench output verified");
        }
        // Exercise steady replacement; transient third blob would fail the
        // entry budget for this fixture, even though two snapshots fit.
        for (int i = 0; i < 5; ++i) {
            record.session.completed_ms = 10000 + i;
            assert(store.save(record) == ESP_OK);
        }
        CertificationStore reloaded;
        assert(reloaded.init() == ESP_OK);
        assert(std::memcmp(&reloaded.current_record(), &store.current_record(), sizeof(record)) == 0);
        const size_t completed = blobs.begin()->second.size();
        assert(initial < 2000 && completed < 4000);
        std::printf("Raw record: %zu bytes; encoded initial: %zu; representative completed: %zu\n",
                    sizeof(record), initial, completed);
        const auto before = store.current_record();
        entry_budget = 0;
        assert(store.save(record) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
        assert(std::memcmp(&before, &store.current_record(), sizeof(record)) == 0);
    } else if (name == "codec_roundtrip_and_bounds") {
        uint32_t random = 0x12345678;
        for (int pattern = 0; pattern < 96; ++pattern) {
            std::vector<uint8_t> bytes(sizeof(CertificationRecord));
            for (size_t i = 0; i < bytes.size(); ++i) {
                random = random * 1664525u + 1013904223u;
                bytes[i] = pattern == 0 ? 0 : pattern == 1 ? 0xff : pattern == 2 ? (i & 1) :
                    pattern == 3 ? (i % 129 == 0 ? 1 : 0) :
                    pattern == 4 ? (i % 130 < 128 ? 1 : 0) : static_cast<uint8_t>(random >> 24);
            }
            const size_t payload_size = lg::encode_zeros(bytes.data(), bytes.size(), nullptr);
            assert(payload_size + sizeof(lg::EncodedHeader) <= lg::kMaxEncodedBytes);
            std::vector<uint8_t> encoded(sizeof(lg::EncodedHeader) + payload_size);
            auto* payload = encoded.data() + sizeof(lg::EncodedHeader);
            assert(lg::encode_zeros(bytes.data(), bytes.size(), payload) == payload_size);
            lg::EncodedHeader header{lg::kEncodedMagic, 1, 0, sizeof(CertificationRecord),
                esp_rom_crc32_le(0, payload, payload_size)};
            std::memcpy(encoded.data(), &header, sizeof(header));
            CertificationRecord decoded;
            assert(lg::decode_record(encoded.data(), encoded.size(), decoded));
            assert(std::memcmp(&decoded, bytes.data(), bytes.size()) == 0);
            encoded.back() ^= 1;
            assert(!lg::decode_record(encoded.data(), encoded.size(), decoded));
        }
    } else if (name == "malformed_encoding") {
        const std::vector<std::vector<uint8_t>> payloads{
            {}, {0}, {127, 1}, {0xff}, std::vector<uint8_t>((sizeof(CertificationRecord) + 127) / 128, 0xff)
        };
        for (const auto& payload : payloads) {
            std::vector<uint8_t> encoded(sizeof(lg::EncodedHeader) + payload.size());
            lg::EncodedHeader header{lg::kEncodedMagic, 1, 0, sizeof(CertificationRecord),
                esp_rom_crc32_le(0, payload.data(), payload.size())};
            std::memcpy(encoded.data(), &header, sizeof(header));
            if (!payload.empty()) std::memcpy(encoded.data() + sizeof(header), payload.data(), payload.size());
            blobs.clear(); blobs["cert_a"] = encoded;
            CertificationStore bad;
            CertificationRecord decoded;
            assert(bad.load(decoded) != ESP_OK && bad.init() == ESP_OK);
            assert(bad.current_record().session.state == CertificationState::NotStarted);
        }
        assert(set_calls == 0 && commit_calls == 0);
    } else { assert(false && "Unknown test case"); }
    std::printf("[PASS] certification store: %s\n", name.c_str());
}
'''

CASES = ["commit_failure", "write_failure", "erase_open_failure", "erase_failure",
         "erase_commit_failure", "single_alternating_write", "recovered_slot_preserved",
         "new_session_sequence", "load_does_not_write", "read_errors_propagate",
         "bad_slot_falls_back", "sequence_wrap", "malformed_record_validation",
         "malformed_save_rejected", "erase_success", "inactive_erase_failure", "inactive_commit_failure",
         "write_after_erase_failure", "final_commit_failure", "encoded_capacity_and_roundtrip",
         "codec_roundtrip_and_bounds", "malformed_encoding"]


def main():
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    if not cxx:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-certification-store-") as directory:
        temp = Path(directory)
        (temp / "support.hpp").write_text(SUPPORT)
        for name in ("esp_err.h", "nvs_flash.h", "nvs.h", "esp_log.h", "esp_rom_crc.h"):
            (temp / name).write_text('#include "support.hpp"\n')
        (temp / "test.cpp").write_text(HARNESS)
        executable = temp / "test"
        subprocess.run([cxx, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I", str(temp), "-I", str(ROOT / "components/certification/include"),
                        "-I", str(ROOT / "components/certification"), str(temp / "test.cpp"),
                        str(ROOT / "components/certification/CertificationPlan.cpp"),
                        "-o", str(executable)], check=True)
        env = dict(os.environ)
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        for case in CASES:
            subprocess.run([str(executable), case], check=True, env=env)


if __name__ == "__main__":
    main()
