// components/certification/CertificationStore.cpp
#include "CertificationStore.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
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

} // namespace

uint32_t CertificationStore::calculate_crc(const CertificationRecord& record) {
    size_t data_len = offsetof(CertificationRecord, checksum);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&record), data_len);
}

esp_err_t CertificationStore::init() {
    if (initialized_) {
        return ESP_OK;
    }

    // LG-038.1: records exceed the startup stack budget; use checked scratch storage.
    std::unique_ptr<CertificationRecord> loaded_buffer(new (std::nothrow) CertificationRecord{});
    if (!loaded_buffer) return ESP_ERR_NO_MEM;
    CertificationRecord& loaded = *loaded_buffer;
    esp_err_t err = load(loaded);
    if (err == ESP_OK) {
        record_ = loaded;
        initialized_ = true;
        ESP_LOGI(TAG, "CertificationStore initialized: cert_id=%s, state=%s, passed=%u, failed=%u",
                 record_.session.identity.certification_id,
                 certification_state_to_string(record_.session.state),
                 record_.session.passed_count,
                 record_.session.failed_count);
        return ESP_OK;
    }

    if (err == ESP_ERR_NO_MEM) return err;

    // Unformatted or not started yet
    reset_certification_record(record_);
    record_.magic = kMagic;
    record_.schema_version = 1;
    record_.sequence = 1;
    record_.session.state = CertificationState::NotStarted;
    record_.checksum = calculate_crc(record_);
    initialized_ = true;

    ESP_LOGI(TAG, "CertificationStore starting fresh (no previous certification record)");
    return ESP_OK;
}

esp_err_t CertificationStore::load(CertificationRecord& out_record) {
    std::unique_ptr<CertificationRecord> slot_a_buffer(new (std::nothrow) CertificationRecord{});
    std::unique_ptr<CertificationRecord> slot_b_buffer(new (std::nothrow) CertificationRecord{});
    if (!slot_a_buffer || !slot_b_buffer) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    CertificationRecord& slot_a = *slot_a_buffer;
    CertificationRecord& slot_b = *slot_b_buffer;
    size_t size_a = sizeof(slot_a);
    size_t size_b = sizeof(slot_b);

    bool a_valid = false;
    bool b_valid = false;

    esp_err_t err_a = nvs_get_blob(handle, kKeySlotA, &slot_a, &size_a);
    if (err_a == ESP_OK && size_a == sizeof(slot_a)) {
        if (slot_a.magic == kMagic && slot_a.schema_version == 1 &&
            slot_a.checksum == calculate_crc(slot_a)) {
            a_valid = true;
        } else {
            ESP_LOGW(TAG, "Slot A failed CRC or header validation");
        }
    }

    esp_err_t err_b = nvs_get_blob(handle, kKeySlotB, &slot_b, &size_b);
    if (err_b == ESP_OK && size_b == sizeof(slot_b)) {
        if (slot_b.magic == kMagic && slot_b.schema_version == 1 &&
            slot_b.checksum == calculate_crc(slot_b)) {
            b_valid = true;
        } else {
            ESP_LOGW(TAG, "Slot B failed CRC or header validation");
        }
    }

    nvs_close(handle);

    if (a_valid && b_valid) {
        // Pick the newer slot by sequence
        if (slot_a.sequence >= slot_b.sequence) {
            out_record = slot_a;
        } else {
            out_record = slot_b;
        }
        return ESP_OK;
    } else if (a_valid) {
        out_record = slot_a;
        // Self-heal slot B
        save(slot_a);
        return ESP_OK;
    } else if (b_valid) {
        out_record = slot_b;
        // Self-heal slot A
        save(slot_b);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t CertificationStore::save(const CertificationRecord& record) {
    std::unique_ptr<CertificationRecord> save_buffer(new (std::nothrow) CertificationRecord(record));
    if (!save_buffer) return ESP_ERR_NO_MEM;
    CertificationRecord& to_save = *save_buffer;
    to_save.magic = kMagic;
    to_save.schema_version = 1;
    to_save.sequence = record_.sequence + 1;
    to_save.checksum = calculate_crc(to_save);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", kNvsNamespace, esp_err_to_name(err));
        return err;
    }

    // Write to alternate or both slots
    const char* target_slot = (to_save.sequence % 2 == 0) ? kKeySlotB : kKeySlotA;
    const char* backup_slot = (to_save.sequence % 2 == 0) ? kKeySlotA : kKeySlotB;

    err = nvs_set_blob(handle, target_slot, &to_save, sizeof(to_save));
    if (err == ESP_OK) {
        // Also mirror to backup slot for high durability
        nvs_set_blob(handle, backup_slot, &to_save, sizeof(to_save));
        nvs_commit(handle);
        record_ = to_save;
        initialized_ = true;
    } else {
        ESP_LOGE(TAG, "Failed to write certification record to %s: %s", target_slot, esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t CertificationStore::erase() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    reset_certification_record(record_);
    record_.magic = kMagic;
    record_.schema_version = 1;
    record_.session.state = CertificationState::NotStarted;
    record_.checksum = calculate_crc(record_);
    ESP_LOGI(TAG, "CertificationStore erased to clean uncertified state");
    return ESP_OK;
}

} // namespace lg
