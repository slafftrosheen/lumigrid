// components/factory/include/cjson_helpers.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <type_traits>
#include <string>
#include <vector>
#include "cJSON.h"

namespace lg {
namespace json {

inline void safe_strncpy(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0 || !dst || !src) return;
    const size_t length = strnlen(src, dst_size - 1);
    std::memmove(dst, src, length);
    std::memset(dst + length, 0, dst_size - length);
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

inline bool is_integer(const cJSON* item) {
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble) &&
           std::trunc(item->valuedouble) == item->valuedouble;
}

template<typename T>
inline bool get_number(const cJSON* root, const char* key, T& dst) {
    if (!root) return false;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item) && std::isfinite(item->valuedouble)) {
        const double value = item->valuedouble;
        if constexpr (std::is_floating_point_v<T>) {
            if (value < std::numeric_limits<T>::lowest() ||
                value > std::numeric_limits<T>::max()) return false;
        } else {
            static_assert(std::is_integral_v<T>, "JSON numeric destinations must be arithmetic");
            // cJSON's valueint saturates at INT_MAX. Use its double value,
            // with an exclusive power-of-two upper bound before narrowing.
            const double upper = std::ldexp(1.0, std::numeric_limits<T>::digits);
            const double lower = std::is_signed_v<T> ? -upper : 0.0;
            if (value < lower || value >= upper || std::trunc(value) != value) return false;
        }
        dst = static_cast<T>(value);
        return true;
    }
    return false;
}

inline bool get_bool(const cJSON* root, const char* key, bool& dst) {
    if (!root) return false;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) {
        dst = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item) && (item->valuedouble == 0 || item->valuedouble == 1)) {
        dst = item->valuedouble == 1;
        return true;
    }
    return false;
}

} // namespace json
} // namespace lg
