// components/control/include/CommandReceipt.hpp
#pragma once
#include <cstdint>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace lg {

enum class CommandDomain : uint8_t {
    Scene,
    Timeline,
    Schedule,
    Config,
    Factory,
    Ota,
    Restore,
    Sync,
    SceneMotion,
    Cue
};

enum class CommandState : uint8_t {
    Accepted,
    Applying,
    Applied,
    Rejected,
    Failed,
    Superseded
};

struct CommandReceipt {
    uint32_t id{0};
    CommandDomain domain{CommandDomain::Scene};
    CommandState state{CommandState::Accepted};
    uint32_t accepted_ms{0};
    uint32_t applied_ms{0};
    int32_t error_code{0};
    uint32_t generation{0};
};

class CommandJournal {
public:
    static constexpr size_t kCapacity = 16;

    uint32_t accept(CommandDomain domain, uint32_t generation = 0);
    void applying(uint32_t id);
    void applied(uint32_t id);
    void failed(uint32_t id, int32_t error);
    void superseded(uint32_t id);
    bool get(uint32_t id, CommandReceipt& out) const;
    bool get_latest(CommandDomain domain, CommandReceipt& out) const;

private:
    static bool newer(uint32_t a, uint32_t b) {
        return static_cast<int32_t>(a - b) > 0;
    }

    mutable std::mutex mutex_;
    std::atomic<uint32_t> next_id_{1};
    CommandReceipt entries_[kCapacity]{};
};

} // namespace lg
