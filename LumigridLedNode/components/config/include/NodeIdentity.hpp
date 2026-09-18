#pragma once
#include <cstdint>
#include <cstddef>

namespace lg {

struct NodeIdentity {
    char id[24];         // e.g. "lg-84f703c29110"
    char name[32];       // Friendly name, e.g. "Lumigrid LED Node"
    char family[16];     // "led-node"
    char model[24];      // "lg-led-v1"
    char hw_revision[8]; // "A"
    char fw_version[16]; // "0.8.0"
};

NodeIdentity get_node_identity(const char* device_name = nullptr);

} // namespace lg
