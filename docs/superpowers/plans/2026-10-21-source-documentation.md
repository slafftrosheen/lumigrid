# Source-backed documentation implementation plan

**Goal:** Rewrite the firmware guides and public LumiGrid site from the current implementation, with one shared documentation source.

**Architecture:** Root `docs/` owns current guides. The site bundles those files at build time. A generated endpoint inventory and capability data keep public facts traceable to firmware. Historical reports retain their original text and are clearly marked as historical.

**Scope:** Both projects in this repository; preserve existing OutputController edits and third-party documentation. No firmware behavior change, hardware flash, release, or deployment.

- [x] Fetch origin and inspect branch, working changes, firmware service owners, limits, authentication, outputs, protocols, storage and dashboard integration.
- [x] Rewrite current guides, preserve historical reports, and add a source/claim audit.
- [x] Replace landing-page content, remove unsupported commercial claims, repair documentation loading and navigation, and explain firmware delivery prerequisites.
- [x] Generate/check shared docs and endpoint inventory; check links, public claims, page rendering, unit tests and production build.

**Review focus:** First-boot versus recovery access; capability flags versus actual enforcement; supported capacity versus measured hardware performance; missing documents and deployment working directories; preservation of pre-existing changes.

## Validation record

- Shared bundle: 60 documents (including history), 88 route registrations, 10 extracted capacities; current guide links and generated-data drift check pass.
- All 28 preserved historical bodies match HEAD after line-ending normalization; only historical-status notices were added.
- `npm run check`: zero errors and zero warnings.
- `npm run test:unit`: 392 tests across 39 files pass, including six documentation contract tests.
- Landing Playwright suite: four tests pass using installed Chromium 1208 via the existing executable-path override. Covers guide navigation/direct load/404, history labels, unavailable release guidance, language selection and 390px layout.
- Desktop and mobile screenshots inspected. Fixed the inherited intrinsic card-width overflow and disabled language selection until hydration completes.
- Final `npm run build`: passes, including Vercel adapter. No deployment performed.
- Firmware invariant and partition-layout scripts pass. No firmware compilation, flashing or physical measurements were needed/performed for these documentation/web changes.
- Strict API surface audit: existing storage-boundary failure in `auth/nodeAuth.ts` and `fleet/registry.ts`; route-level storage, legacy API and WebSocket checks pass. Recorded in the source audit without modifying authentication behavior.
- Fifty unreachable legacy public components, unused marketing data/dictionaries, missing-manifest installer and obsolete sitemap claims removed. Active dashboard modules retained.
- Pre-existing OutputController edits preserved. Changes remain local and uncommitted; no push or release.
