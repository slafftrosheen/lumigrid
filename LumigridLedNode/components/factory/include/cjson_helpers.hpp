// components/factory/include/cjson_helpers.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "cJSON.h"

namespace lg {
namespace json {

inline void safe_strncpy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0 || !dst || !src) return;
    std::memcpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

inline bool get_string(const cJSON* root, const char* key, char* dst, size_t dst_size) {
    if (!root) return false;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        safe_strncpy(dst, item->valuestring, dst_size);
        return true;
    }
    return false;
}

template<typename T>
inline bool get_number(const cJSON* root, const char* key, T& dst) {
    if (!root) return false;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        if constexpr (std::is_floating_point_v<T>) {
            dst = static_cast<T>(item->valuedouble);
        } else {
            dst = static_cast<T>(item->valueint);
        }
        return true;
    }
    return false;
}

inline bool get_bool(const cJSON* root, const char* key, bool& dst) {
    if (!root) return false;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item) || cJSON_IsNumber(item)) {
        dst = cJSON_IsTrue(item);
        return true;
    }
    return false;
}

} // namespace json
} // namespace lg
