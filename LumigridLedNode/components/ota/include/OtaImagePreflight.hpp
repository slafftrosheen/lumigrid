#pragma once
#include <cstddef>
#include <cstring>
#include "esp_app_format.h"
#include "esp_app_desc.h"
namespace lg {
constexpr size_t kOtaDescriptorPrefixBytes = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
inline bool ota_app_descriptor_valid(const esp_app_desc_t& candidate) {
    return candidate.magic_word == ESP_APP_DESC_MAGIC_WORD &&
        memchr(candidate.project_name, 0, sizeof(candidate.project_name)) &&
        strcmp(candidate.project_name, "lumigrid_led_node") == 0 && candidate.version[0] &&
        memchr(candidate.version, 0, sizeof(candidate.version));
}
inline bool ota_image_prefix_valid(const void* prefix, size_t bytes, uint16_t chip_revision, esp_app_desc_t* candidate) {
    if (!prefix || bytes < kOtaDescriptorPrefixBytes || !candidate) return false;
    esp_image_header_t image{};
    esp_image_segment_header_t segment{};
    memcpy(&image, prefix, sizeof(image));
    memcpy(&segment, static_cast<const uint8_t*>(prefix) + sizeof(image), sizeof(segment));
    memcpy(candidate, static_cast<const uint8_t*>(prefix) + sizeof(image) + sizeof(segment), sizeof(*candidate));
    const uint16_t minimum = image.min_chip_rev_full ? image.min_chip_rev_full : image.min_chip_rev * 100;
    const bool revision_ok = chip_revision >= minimum && (!image.max_chip_rev_full || image.max_chip_rev_full == 0xffff || chip_revision <= image.max_chip_rev_full);
    return image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32 &&
        image.segment_count > 0 && image.segment_count <= 16 && image.spi_size == ESP_IMAGE_FLASH_SIZE_8MB &&
        revision_ok && segment.data_len >= sizeof(esp_app_desc_t) && ota_app_descriptor_valid(*candidate);
}
}
