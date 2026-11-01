#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
namespace host {
inline std::atomic<int64_t> timer_us{100000000};
inline thread_local unsigned critical_depth = 0;
}
