# Configuration

`DeviceConfig` schema **4** is current. `ConfigStore` owns validation, migration and persistence. Network settings use schema **2**; authentication has separate storage from deprecated device credential fields.

Configure 16 channels and up to eight groups. Addressable channels have type, order and pixel count; all channels have enable, brightness, gamma and DMX patch fields. A name alone does not configure a channel: `channel_is_configured()` checks output-relevant fields.

Use GET `/api/v1/config`, POST `/api/v1/config/validate`, then PUT `/api/v1/config`. Inspect validation and command results. Applying may reconfigure outputs/services; acceptance does not prove completion.

| Model field | Declared default |
| --- | --- |
| Name / timezone | `lumigrid` / `UTC0` |
| Gamma | `gamma_x10 = 22` (2.2) |
| Soft start | enabled, 500 ms |
| Art-Net / sACN / MQTT / sync | disabled |
| DMX merge / timeout / layer | HTP / 2000 ms / 2 |
| MQTT port | 1883 |
| Global power ceiling | 0 (no configured ceiling) |

Defaults do not replace saved settings. `reset()` clears nested channel/group/supply storage explicitly; read normalized node configuration instead of assuming every C++ initializer survives reset.

## Power model

Units are milliwatts, millivolts and milliamps. `rated_full_power_mw` describes PWM full-duty load; `channel_limit_mw` is the local ceiling; `max_milliwatts` is a compatibility field. Up to four logical supply domains assign channel masks and derive usable power from voltage, rated current and usable-capacity permille.

`PowerBudget` applies channel/supply/global limits and soft start. Model states are `complete`, `partial`, `unknown`; missing data is not zero consumption. These are estimates, not sensors or electrical/thermal certification.

## Network

Modes: `ap_only`, `sta_only`, `sta_with_ap_fallback`. Apply states: `idle`, `applying`, `waiting_for_network`, `rolling_back`, `failed`. Use `/api/v1/network/config` and `/api/v1/network/status`; preserve recovery access while changing connection details. AP passwords differ from Admin keys.

Sources: `LumigridLedNode/components/config/ConfigStore.cpp`, `ConfigApplyPlan.cpp`, `include/SignageTypes.hpp`, `NetworkSettings.hpp`, `ChannelConfiguration.hpp`, and `components/signage/PowerBudget.cpp`.
