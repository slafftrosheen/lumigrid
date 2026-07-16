#pragma once
#include <cstddef>
#include <cstdint>
#include <config/lg_config.hpp>

namespace lg {

enum class ConfigError : uint8_t {
    None,
    InvalidSchema,
    InvalidLane,
    InvalidPwmGroup,
    PwmOverlap,
    InvalidNetwork,
    StorageFailure
};

class ConfigStore {
public:
    esp_err_t init();
    esp_err_t load(NodeConfig& out);
    ConfigError validate(const NodeConfig& cfg) const;
    esp_err_t save(const NodeConfig& cfg);
    esp_err_t reset_to_factory(NodeConfig& out);

private:
    static constexpr const char* kNamespace = "lumigrid";
    static constexpr const char* kKey = "node_config";
};

}
