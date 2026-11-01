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

`PowerBudget` and the final output guard share conservative command estimates: 5 mW of fixed idle per configured addressable pixel, 295 mW per full-scale RGB component and 380 mW per full-scale white component. Full RGB therefore estimates 890 mW per pixel, and full RGBW estimates 1,270 mW. PWM uses its configured full-duty load rating. These assumptions preserve the final guard's previous model and still require calibration against the actual fixtures.

Channel, supply and global ceilings scale active demand, followed by soft start and transient limiting. Each stage re-estimates the quantized commands; fractional loads remain exact until aggregate reporting rounds upward. Requested power represents the command before these limits, while limited power and supply loads represent the final confirmed driver commands. Renderer previews can differ from final output brightness.

A zero channel/global ceiling means no ceiling at that tier. A disabled supply domain is ignored; an **enabled supply with zero usable capacity** extinguishes its assigned active outputs, including unrated PWM. Fixed addressable idle remains in the estimate. If a ceiling is below that idle floor, the estimate can exceed the ceiling even with all active components zero.

Model states are `complete`, `partial`, `unknown`; missing ratings are not zero consumption. Positive budgets cannot fully constrain an unrated PWM load. Failed output plus failed blackout retains the last confirmed command estimate and marks it incomplete/unknown until a confirmed operation. `output.power.estimateComplete` qualifies the runtime snapshot estimate; measured power remains unavailable. These are software estimates, not current sensors or electrical/thermal certification.

## Network

Modes: `ap_only`, `sta_only`, `sta_with_ap_fallback`. Apply states: `idle`, `applying`, `waiting_for_network`, `rolling_back`, `failed`. Use `/api/v1/network/config` and `/api/v1/network/status`; preserve recovery access while changing connection details. AP passwords differ from Admin keys.

Sources: `LumigridLedNode/components/config/ConfigStore.cpp`, `ConfigApplyPlan.cpp`, `include/SignageTypes.hpp`, `NetworkSettings.hpp`, `ChannelConfiguration.hpp`, and `components/signage/PowerBudget.cpp`.
