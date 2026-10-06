#pragma once
#include <cstdint>
#include "esp_ota_ops.h"

namespace lg {
constexpr uint16_t kPartitionLayoutVersion = 2;
constexpr uint32_t kProductionFlashBytes = 0x800000;
constexpr uint32_t kProductionAppSlotBytes = 0x250000;
constexpr uint32_t kProductionLayoutEnd = 0x800000;

enum class StorageHealth : uint8_t { Healthy, FlashCapacityMismatch, PartitionLayoutMismatch, OtaSlotTooSmall };
inline StorageHealth evaluate_storage_health(uint32_t physical, uint32_t configured,
                                            bool layout_matches, uint32_t next_slot) {
    if (physical < configured || physical < kProductionLayoutEnd || configured < kProductionFlashBytes)
        return StorageHealth::FlashCapacityMismatch;
    if (!layout_matches) return StorageHealth::PartitionLayoutMismatch;
    if (next_slot < kProductionAppSlotBytes) return StorageHealth::OtaSlotTooSmall;
    return StorageHealth::Healthy;
}
inline const char* storage_health_name(StorageHealth health) {
    switch (health) {
    case StorageHealth::Healthy: return "HEALTHY";
    case StorageHealth::FlashCapacityMismatch: return "FLASH_CAPACITY_MISMATCH";
    case StorageHealth::PartitionLayoutMismatch: return "PARTITION_LAYOUT_MISMATCH";
    case StorageHealth::OtaSlotTooSmall: return "OTA_SLOT_TOO_SMALL";
    }
    return "PARTITION_LAYOUT_MISMATCH";
}
struct StorageSnapshot {
    uint32_t physical_bytes = 0;
    uint32_t configured_bytes = 0;
    uint32_t image_bytes = 0;
    const esp_partition_t* running = nullptr;
    const esp_partition_t* next = nullptr;
    StorageHealth health = StorageHealth::FlashCapacityMismatch;
};
StorageSnapshot capture_storage();
}
