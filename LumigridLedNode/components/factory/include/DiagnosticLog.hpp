#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <cstdarg>
#include "esp_log.h"

namespace lg {

class DiagnosticLog {
public:
    static constexpr size_t kBufferSize = 4096;

    static DiagnosticLog& instance() {
        static DiagnosticLog inst;
        return inst;
    }

    void init();
    
    // Writes log content into a provided buffer, null-terminated
    size_t copy_logs(char* out_buffer, size_t max_len);

    using LogCallback = void(*)(const char* msg, size_t len);
    void set_stream_callback(LogCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_cb_ = cb;
    }

private:
    DiagnosticLog() = default;
    ~DiagnosticLog() = default;

    static int vprintf_handler(const char *fmt, va_list args);
    int handle_log(const char *fmt, va_list args);

    char buffer_[kBufferSize] = {};
    size_t head_ = 0;
    bool wrapped_ = false;
    std::mutex mutex_;
    vprintf_like_t default_vprintf_ = nullptr;
    LogCallback stream_cb_ = nullptr;
};

} // namespace lg
