#include "StorageContract.hpp"
#include "esp_flash.h"
#include "esp_image_format.h"

namespace lg {
StorageSnapshot capture_storage() {
    StorageSnapshot snap{};
    esp_flash_get_physical_size(nullptr, &snap.physical_bytes);
    esp_flash_get_size(nullptr, &snap.configured_bytes);
    snap.running = esp_ota_get_running_partition();
    snap.next = esp_ota_get_next_update_partition(nullptr);
    struct Expected { const char* name; uint8_t type; uint8_t subtype; uint32_t address; uint32_t size; };
    const Expected expected[] = {
        {"nvs",1,2,0x9000,0x4000}, {"otadata",1,0,0xd000,0x2000},
        {"phy_init",1,1,0xf000,0x1000}, {"factory",0,0,0x10000,0x250000},
        {"ota_0",0,0x10,0x260000,0x250000}, {"ota_1",0,0x11,0x4b0000,0x250000},
        {"littlefs",1,0x82,0x700000,0x100000}
    };
    bool matches = true;
    for (const auto& e : expected) {
        const auto* p = esp_partition_find_first(static_cast<esp_partition_type_t>(e.type), static_cast<esp_partition_subtype_t>(e.subtype), e.name);
        if (!p || p->address != e.address || p->size != e.size) matches = false;
    }
    snap.health = evaluate_storage_health(snap.physical_bytes, snap.configured_bytes, matches, snap.next ? snap.next->size : 0);
    if (snap.running) {
        esp_image_metadata_t metadata{};
        const esp_partition_pos_t pos{snap.running->address, snap.running->size};
        if (esp_image_get_metadata(&pos, &metadata) == ESP_OK) snap.image_bytes = metadata.image_len;
    }
    return snap;
}
}
