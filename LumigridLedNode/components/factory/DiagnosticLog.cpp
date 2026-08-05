#include "DiagnosticLog.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace lg {

void DiagnosticLog::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!default_vprintf_) {
        default_vprintf_ = esp_log_set_vprintf(vprintf_handler);
    }
}

int DiagnosticLog::vprintf_handler(const char *fmt, va_list args) {
    return instance().handle_log(fmt, args);
}

int DiagnosticLog::handle_log(const char *fmt, va_list args) {
    // Format into a temporary buffer
    char temp[256];
    
    // We need to copy args because vsnprintf mutates it, and we need it for default_vprintf_
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(temp, sizeof(temp), fmt, args_copy);
    va_end(args_copy);

    if (len > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t write_len = std::min<size_t>(len, sizeof(temp) - 1);
        
        for (size_t i = 0; i < write_len; ++i) {
            buffer_[head_] = temp[i];
            head_++;
            if (head_ >= kBufferSize) {
                head_ = 0;
                wrapped_ = true;
            }
        }
        
        if (stream_cb_) {
            stream_cb_(temp, write_len);
        }
    }

    if (default_vprintf_) {
        return default_vprintf_(fmt, args);
    }
    return len;
}

size_t DiagnosticLog::copy_logs(char* out_buffer, size_t max_len) {
    if (!out_buffer || max_len == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    size_t write_idx = 0;

    if (wrapped_) {
        // Read from head_ to end
        for (size_t i = head_; i < kBufferSize && write_idx < max_len - 1; ++i) {
            out_buffer[write_idx++] = buffer_[i];
        }
    }

    // Read from 0 to head_
    for (size_t i = 0; i < head_ && write_idx < max_len - 1; ++i) {
        out_buffer[write_idx++] = buffer_[i];
    }

    out_buffer[write_idx] = '\0';
    return write_idx;
}

} // namespace lg
