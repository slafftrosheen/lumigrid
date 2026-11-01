# Source-of-truth audit

Audit date: 2026-10-21. Baseline `8afd6f04eb9fed6db82af102db07bfbed3e120ed`, branch `codex/lg-p0-boot-001`. Origin was fetched; main was already an ancestor, four commits behind. Pre-existing OutputController.cpp/.hpp edits were preserved and included in working-tree inspection. This is not hardware qualification.

The effect/palette capacity row below follows the current catalogue. The audit findings retain the baseline and evidence scope above.

| Public fact | Authority under firmware components | Boundary |
| --- | --- | --- |
| 8 addressable + 8 PWM | config/include/OutputCapabilities.hpp, board/include/lg_board_pins.hpp | Eight independent dimmers, not all PCA chip outputs |
| WS2812 RGB / SK6812 RGBW | config/include/SignageTypes.hpp, output/ | Correct order/wiring and memory admission |
| 32 scenes, 8 zones, 3 layers | config/include/Capabilities.hpp, SignageTypes.hpp | Capacity, not throughput |
| 16 timeline steps; 8 × 16 cues | Same headers, scheduler/, show/ | Valid matching content |
| 24 effects, 14 palettes | config/include/SignageTypes.hpp: signage_effect_metadata and signage_palette_metadata; signage/EffectRenderer.cpp | Per-effect controls and PWM suitability; see [Effects](EFFECTS.md) |
| Local control | factory/FactoryApi.cpp, web API/embedded sources | HTTP/WS browser restrictions |
| Art-Net/sACN | protocols/, service planner | Disabled by default |
| Node sync | sync/NodeSync.cpp, RuntimeServices.hpp | Shared LAN, no accuracy guarantee |
| Startup scene loading | signage/SceneManager.cpp | Saved scene, slot zero, then blackout fallback |
| Power budget | signage/PowerBudget.cpp | Estimate, not sensor |
| OTA/rollback | post_ota, ota/ | Suitable layout/image and boot health |
| Certification tools | certification/ | Internal evidence workflow |

## Public corrections

The old landing page mixed capabilities with unsupported installation/support commitments, generic installation images, testimonials and absolute reliability/synchronization/flicker claims. Current content describes implemented workflows and links to guides. Preorder structured data is removed because source does not establish a commercial offer.

The old docs index linked missing Node Family, Embedded Integration, StyleSystem and Contributing pages. Its reader used deployment-relative filesystem paths and lacked a page loader. Current docs are bundled from the shared source with deterministic pages and validated links.

## Open gaps

- At the audit baseline, `kFeatureSignedOta` was true without established signature enforcement. The 2026-10-23 candidate now derives this flag from `CONFIG_SECURE_SIGNED_ON_UPDATE`; the default build correctly reports false. A signed production provisioning/update process remains open. See [candidate evidence](FIRMWARE_RELEASE_CANDIDATE.md).
- MQTT lacks a dedicated broker credential/certificate provisioning workflow in the reviewed service.
- Browser flasher referenced an absent release manifest; the page now explains installation prerequisites.
- Physical timing, combined-load endurance, electrical/thermal behavior and current-revision OTA recovery require target evidence.
- Historical reports remain scoped to their original build/context.
- At the audit baseline, the strict API-boundary audit found direct storage access in `src/lib/fleet/registry.ts` and `src/lib/auth/nodeAuth.ts`. The 2026-11-01 continuity batch moves those accesses behind guarded `auth/storage.ts` helpers, including session-only credential storage. The strict audit now passes; denied browser persistence is covered by the auth/storage regressions.

Change source first, then guides/translations. Regenerate/check docs to detect route/capacity drift. Record fresh hardware evidence separately instead of rewriting an older report as though rerun.
