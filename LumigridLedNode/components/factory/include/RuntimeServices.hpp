// components/factory/include/RuntimeServices.hpp
//
// LG-038.3 §61: one deterministic, pure service planner.
//
// The plan is derived from three inputs only — Safe Mode, commissioning state,
// network state — plus DeviceConfig. It replaces scattered lifecycle `if`s:
// startup, config apply and network rebind all compute a plan and reconcile
// start/stop differences against it (§62, §64, §65).
//
// Factory worker and network-apply worker are intentionally NOT in the plan:
// both are lazy (LG-038.3 §5, §18, §8) and only start on explicit use.
#pragma once

#include <cstdint>
#include <cstddef>

#include "SignageTypes.hpp"

namespace lg {

enum class CommissioningPlanState : uint8_t {
    Provisioned = 0,
    FactoryFirstBoot = 1,   // LumiGrid-Setup AP, temporary setup credential active
    PhysicalRecovery = 2,
};

enum class NetworkPlanState : uint8_t {
    NoNetwork = 0,   // no usable interface yet
    ApOnly = 1,      // private setup/recovery AP, no shared LAN
    StaConnected = 2,
    StaDown = 3,     // fallback AP after STA loss
};

struct RuntimeServicePlan {
    bool lighting = false;
    bool scheduler = false;

    bool api = false;
    bool mdns = false;
    bool fleet_discovery = false;

    bool artnet = false;
    bool sacn = false;
    bool node_sync = false;
    bool mqtt = false;

    // Factory worker / network apply are lazy-on-demand and never planned true
    // at boot (LG-038.3 §18, §8, §20).
    bool factory_worker = false;
    bool network_apply_worker = false;
};

struct PlanInputs {
    bool safe_mode = false;
    CommissioningPlanState commissioning = CommissioningPlanState::Provisioned;
    NetworkPlanState network = NetworkPlanState::NoNetwork;
    const DeviceConfig* cfg = nullptr;
};

inline RuntimeServicePlan compute_service_plan(const PlanInputs& in) {
    RuntimeServicePlan plan{};

    // First-boot setup (LG-038.3 §35, §66): API + mDNS on the setup AP only.
    // Every output/protocol service stays off until the user configures it.
    if (in.commissioning == CommissioningPlanState::FactoryFirstBoot) {
        plan.lighting = false;
        plan.scheduler = false;
        plan.api = true;
        plan.mdns = true;
        return plan;
    }

    // Recovery / safe mode plan (LG-038.3 §63): outputs safe, API + mDNS kept,
    // all network-dependent services off. Recovery Wi-Fi is handled outside
    // the plan (it is always permitted, never optional).
    if (in.safe_mode || in.commissioning == CommissioningPlanState::PhysicalRecovery) {
        plan.lighting = false;
        plan.scheduler = false;
        plan.api = true;
        plan.mdns = true;
        plan.fleet_discovery = false;
        plan.artnet = false;
        plan.sacn = false;
        plan.node_sync = false;
        plan.mqtt = false;
        plan.factory_worker = false;
        return plan;
    }

    const bool has_cfg = (in.cfg != nullptr);

    // Normal configured node (LG-038.3 §62).
    plan.lighting = true;
    plan.scheduler = true;
    plan.api = true;
    plan.mdns = true;

    // Fleet discovery only when the node actually shares a LAN (§21-22).
    plan.fleet_discovery = (in.network == NetworkPlanState::StaConnected);

    plan.artnet    = has_cfg && in.cfg->protocol.artnet_enabled;
    plan.sacn      = has_cfg && in.cfg->protocol.sacn_enabled;
    // Sync needs shared-LAN transport; a fallback AP with no uplink cannot
    // carry it, so only a genuinely connected STA satisfies the plan (§65).
    plan.node_sync = has_cfg && in.cfg->sync_enabled &&
                     in.network == NetworkPlanState::StaConnected;
    plan.mqtt      = has_cfg && in.cfg->mqtt_enabled && in.cfg->mqtt_broker[0] != '\0';

    plan.factory_worker = false;       // lazy (§18-19)
    plan.network_apply_worker = false; // lazy (§8)
    return plan;
}

// LG-038.3 §60: observed service truth (not the plan). Exposed through
// RuntimeSnapshot "services" and /api/v1/diagnostics/memory so RAM use is
// explainable per service.
struct ServiceStateTruth {
    bool lighting = false;
    bool scheduler = false;
    bool api = false;
    bool mdns = false;
    bool fleet_discovery = false;
    bool artnet = false;
    bool sacn = false;
    bool node_sync = false;
    bool mqtt = false;
    bool factory_worker = false;
    bool network_apply_worker = false;
};

} // namespace lg
