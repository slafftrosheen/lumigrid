// components/certification/CertificationStore.cpp
#include "CertificationStore.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

namespace lg {

namespace {

static const char* TAG = "cert_store";
static const char* kNvsNamespace = "hw_cert";
static const char* kKeySlotA = "cert_a";
static const char* kKeySlotB = "cert_b";
static constexpr uint32_t kMagic = 0x4C474354; // "LGCT"
static constexpr uint32_t kEncodedMagic = 0x4C475A52; // "LGZR"

struct EncodedHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t decoded_size;
    uint32_t payload_crc;
};
static_assert(sizeof(EncodedHeader) == 16, "Encoded certification header layout changed");
static constexpr size_t kMaxEncodedBytes = sizeof(EncodedHeader) + sizeof(CertificationRecord) +
    (sizeof(CertificationRecord) + 127) / 128;

// Version 1 encoding: high-bit tokens denote 1..128 zero bytes; otherwise
// 1..128 literal bytes follow. Only zero runs of at least two bytes are emitted,
// so worst-case output adds at most one token per 128 input bytes. No fields,
// precision, notes or evidence are discarded. Passing nullptr measures first,
// allowing one small allocation for typical sparse/partly completed records.
size_t encode_zeros(const uint8_t* input, size_t length, uint8_t* output) {
    size_t in = 0, out = 0;
    while (in < length) {
        const bool zeros = input[in] == 0 && in + 1 < length && input[in + 1] == 0;
        const size_t start = in++;
        while (in < length && in - start < 128) {
            if (zeros ? input[in] != 0 : (input[in] == 0 && in + 1 < length && input[in + 1] == 0)) break;
            ++in;
        }
        const size_t count = in - start;
        if (output) output[out] = static_cast<uint8_t>((zeros ? 0x80 : 0) | (count - 1));
        ++out;
        if (!zeros) {
            if (output) std::memcpy(output + out, input + start, count);
            out += count;
        }
    }
    return out;
}

bool decode_record(const uint8_t* stored, size_t length, CertificationRecord& record) {
    if (length < sizeof(uint32_t)) return false;
    uint32_t magic;
    std::memcpy(&magic, stored, sizeof(magic));
    if (magic == kMagic) {
        // Read-only compatibility with raw schema 2. Header/schema/semantic
        // checks below still reject old or truncated schema 1 records.
        if (length != sizeof(record)) return false;
        std::memcpy(&record, stored, sizeof(record));
        return true;
    }
    if (length <= sizeof(EncodedHeader) || length > kMaxEncodedBytes) return false;
    EncodedHeader header;
    std::memcpy(&header, stored, sizeof(header));
    const uint8_t* payload = stored + sizeof(header);
    const size_t payload_size = length - sizeof(header);
    if (header.magic != kEncodedMagic || header.version != 1 || header.reserved != 0 ||
        header.decoded_size != sizeof(record) ||
        header.payload_crc != esp_rom_crc32_le(0, payload, payload_size)) return false;
    auto* output = reinterpret_cast<uint8_t*>(&record);
    size_t in = 0, out = 0;
    while (in < payload_size) {
        const uint8_t token = payload[in++];
        const size_t count = (token & 0x7f) + 1;
        if (count > sizeof(record) - out) return false;
        if (token & 0x80) {
            std::memset(output + out, 0, count);
        } else {
            if (count > payload_size - in) return false;
            std::memcpy(output + out, payload + in, count);
            in += count;
        }
        out += count;
    }
    return out == sizeof(record);
}

template<size_t N>
bool terminated(const char (&text)[N]) {
    return std::memchr(text, '\0', N) != nullptr;
}

bool valid_record_data(const CertificationRecord& record) {
    const auto& session = record.session;
    const auto& identity = session.identity;
    if (record.result_count > kMaxCertificationTests || session.test_count != record.result_count ||
        static_cast<uint8_t>(session.state) > static_cast<uint8_t>(CertificationState::Invalidated) ||
        static_cast<uint8_t>(session.profile) > static_cast<uint8_t>(CertificationProfile::ReleaseCandidate) ||
        session.passed_count > record.result_count || session.failed_count > record.result_count ||
        session.skipped_count > record.result_count ||
        session.passed_count + session.failed_count + session.skipped_count > record.result_count ||
        !terminated(identity.certification_id) || !terminated(identity.node_id) ||
        !terminated(identity.firmware_version) || !terminated(identity.project_name) ||
        !terminated(identity.hardware_revision) || !terminated(identity.serial_number)) {
        return false;
    }
    if (session.state == CertificationState::NotStarted) {
        return record.result_count == 0;
    }
    if (record.result_count != CertificationPlan::test_count() ||
        identity.plan_version != kCertificationPlanVersion) return false;

    bool seen[kMaxCertificationTests]{};
    for (size_t i = 0; i < record.result_count; ++i) {
        const auto& result = record.results[i];
        if (!terminated(result.test_id) || !terminated(result.unit) || !terminated(result.note) ||
            !std::isfinite(result.measured_value) ||
            static_cast<uint8_t>(result.state) > static_cast<uint8_t>(CertificationTestState::Blocked) ||
            static_cast<uint8_t>(result.evidence_source) > static_cast<uint8_t>(EvidenceSource::InstrumentMeasurement)) {
            return false;
        }
        const int index = CertificationPlan::find_test_index(result.test_id);
        if (index < 0 || static_cast<size_t>(index) >= kMaxCertificationTests || seen[index]) return false;
        seen[index] = true;
        if (session.state == CertificationState::Passed &&
            CertificationPlan::get_requirement(result.test_id, session.profile) == RequirementLevel::Required &&
            result.state != CertificationTestState::Passed) return false;
    }
    return true;
}

} // namespace

uint32_t CertificationStore::calculate_crc(const CertificationRecord& record) {
    size_t data_len = offsetof(CertificationRecord, checksum);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&record), data_len);
}

esp_err_t CertificationStore::init() {
    if (initialized_) {
        return ESP_OK;
    }

    // load() uses one record-sized heap buffer plus bounded encoded input;
    // avoid another record-sized allocation (and all record-sized stack frames).
    esp_err_t err = load(record_);
    if (err == ESP_OK) {
        initialized_ = true;
        ESP_LOGI(TAG, "CertificationStore initialized: cert_id=%s, state=%s, passed=%u, failed=%u",
                 record_.session.identity.certification_id,
                 certification_state_to_string(record_.session.state),
                 record_.session.passed_count,
                 record_.session.failed_count);
        return ESP_OK;
    }

    reset_certification_record(record_);
    if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_NVS_NOT_FOUND &&
        err != ESP_ERR_INVALID_SIZE && err != ESP_ERR_NVS_INVALID_LENGTH && err != ESP_ERR_INVALID_CRC) {
        return err; // Storage failures must not masquerade as a clean first boot.
    }

    // Missing, corrupt and older schemas start uncertified; preserve their blobs
    // for diagnosis rather than migrating old results into a passing session.
    record_.magic = kMagic;
    record_.schema_version = kCertificationRecordSchemaVersion;
    active_slot_ = -1;
    committed_sequence_ = 0;
    record_.session.state = CertificationState::NotStarted;
    record_.checksum = calculate_crc(record_);
    initialized_ = true;

    ESP_LOGI(TAG, "CertificationStore starting fresh (no previous certification record)");
    return ESP_OK;
}

esp_err_t CertificationStore::load(CertificationRecord& out_record) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    std::unique_ptr<CertificationRecord> buffer(new (std::nothrow) CertificationRecord{});
    if (!buffer) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    const char* keys[] = {kKeySlotA, kKeySlotB};
    int8_t selected = -1;
    esp_err_t invalid = ESP_ERR_NOT_FOUND;
    for (int8_t slot = 0; slot < 2; ++slot) {
        size_t size = 0;
        err = nvs_get_blob(handle, keys[slot], nullptr, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err == ESP_ERR_NVS_INVALID_LENGTH || (err == ESP_OK && (size == 0 || size > kMaxEncodedBytes))) {
            invalid = ESP_ERR_INVALID_SIZE;
            continue;
        }
        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }
        std::unique_ptr<uint8_t[]> encoded(new (std::nothrow) uint8_t[size]);
        if (!encoded) {
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }
        const size_t capacity = size;
        err = nvs_get_blob(handle, keys[slot], encoded.get(), &size);
        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }
        if (size != capacity || !decode_record(encoded.get(), size, *buffer)) {
            invalid = ESP_ERR_INVALID_CRC;
            continue;
        }
        const auto& candidate = *buffer;
        if (candidate.magic != kMagic || candidate.schema_version != kCertificationRecordSchemaVersion ||
            candidate.checksum != calculate_crc(candidate) || !valid_record_data(candidate)) {
            ESP_LOGW(TAG, "Certification slot %s rejected (schema, CRC or record data)", keys[slot]);
            invalid = ESP_ERR_INVALID_CRC;
            continue;
        }
        // Unsigned subtraction handles sequence rollover; only adjacent saved
        // generations are expected in the two slots.
        const uint32_t delta = candidate.sequence - out_record.sequence;
        if (selected < 0 || (delta != 0 && delta < 0x80000000u)) {
            out_record = candidate;
            selected = slot;
        }
    }
    nvs_close(handle);
    if (selected < 0) return invalid;
    active_slot_ = selected;
    committed_sequence_ = out_record.sequence;
    return ESP_OK; // Loading never writes or attempts a costly mirror repair.
}

esp_err_t CertificationStore::save(const CertificationRecord& record) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    if (!valid_record_data(record)) return ESP_ERR_INVALID_ARG;
    std::unique_ptr<CertificationRecord> save_buffer(new (std::nothrow) CertificationRecord(record));
    if (!save_buffer) return ESP_ERR_NO_MEM;
    CertificationRecord& to_save = *save_buffer;
    to_save.magic = kMagic;
    to_save.schema_version = kCertificationRecordSchemaVersion;
    to_save.sequence = committed_sequence_ + 1;
    to_save.checksum = calculate_crc(to_save);

    // A raw 46-result record is about 8 KB; two raw slots cannot fit the
    // preserved 16 KB NVS partition. Encode sparse data without changing that
    // deployed partition layout. Full/high-entropy stores still report NVS
    // capacity errors to the caller; no automatic erase or truncation occurs.
    const auto* bytes = reinterpret_cast<const uint8_t*>(&to_save);
    const size_t payload_size = encode_zeros(bytes, sizeof(to_save), nullptr);
    const size_t stored_size = sizeof(EncodedHeader) + payload_size;
    if (stored_size > kMaxEncodedBytes) return ESP_ERR_INVALID_SIZE;
    std::unique_ptr<uint8_t[]> encoded(new (std::nothrow) uint8_t[stored_size]);
    if (!encoded) return ESP_ERR_NO_MEM;
    encode_zeros(bytes, sizeof(to_save), encoded.get() + sizeof(EncodedHeader));
    const EncodedHeader header{kEncodedMagic, 1, 0, sizeof(to_save),
        esp_rom_crc32_le(0, encoded.get() + sizeof(EncodedHeader), payload_size)};
    std::memcpy(encoded.get(), &header, sizeof(header));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", kNvsNamespace, esp_err_to_name(err));
        return err;
    }

    // Keep the last committed generation intact. Mirroring both large blobs
    // discards that fallback and needlessly consumes NVS write capacity.
    const int8_t target_index = active_slot_ == 0 ? 1 : 0;
    const char* target_slot = target_index == 0 ? kKeySlotA : kKeySlotB;

    // NVS retains the old value of a key while writing its replacement. Drop
    // only this obsolete/inactive generation first, avoiding three live copies
    // in the small partition. The active committed slot is never touched.
    err = nvs_erase_key(handle, target_slot);
    if (err == ESP_OK) err = nvs_commit(handle);
    else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    err = nvs_set_blob(handle, target_slot, encoded.get(), stored_size);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        record_ = to_save;
        active_slot_ = target_index;
        committed_sequence_ = to_save.sequence;
    } else {
        ESP_LOGE(TAG, "Failed to write certification record to %s: %s", target_slot, esp_err_to_name(err));
        // set_blob may have reached flash before commit reports an error. Do
        // not intentionally leave an unconfirmed newer generation selectable
        // on reboot. Cleanup is best-effort under a failing flash device.
        esp_err_t cleanup = nvs_erase_key(handle, target_slot);
        if (cleanup == ESP_OK) cleanup = nvs_commit(handle);
        if (cleanup != ESP_OK && cleanup != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to discard unconfirmed certification slot: %s", esp_err_to_name(cleanup));
        }
    }

    nvs_close(handle);
    return err;
}

esp_err_t CertificationStore::erase() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    reset_certification_record(record_);
    record_.magic = kMagic;
    record_.schema_version = kCertificationRecordSchemaVersion;
    active_slot_ = -1;
    committed_sequence_ = 0;
    initialized_ = true;
    record_.session.state = CertificationState::NotStarted;
    record_.checksum = calculate_crc(record_);
    ESP_LOGI(TAG, "CertificationStore erased to clean uncertified state");
    return ESP_OK;
}

} // namespace lg
