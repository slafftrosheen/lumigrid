# Development and maintenance

Work from the repository containing both projects. Check branch/remote/dirty files and preserve unrelated work. A standalone copy of the web folder omits shared docs and firmware build inputs.

The documentation sync hooks require Python 3.9+ on PATH as `python`, alongside Node.js. Firmware builds additionally require PlatformIO and its pinned platform/toolchain.

## Web

From `lumigrid-lander-main/`, with Node.js 20+:

```powershell
npm ci
npm run dev
npm run check
npm run test:unit
npm run build
```

`check` combines Svelte sync, copy lint and Svelte checking; report its failures separately from tests/builds. `npm run test` uses Playwright: inspect its server requirements first. The production adapter is Vercel; a build does not deploy.

## Firmware

After web dependencies are installed, from `LumigridLedNode/`:

```powershell
pio run -e factory
python scripts/check_invariants.py
python scripts/verify_partition_layout.py
python scripts/verify_runtime_math.py
```

`factory` is production. `test` enables runtime tests/development credentials. `rollback-test` intentionally requests OTA rollback. Neither test environment is a production release. Serial defaults are not device discovery.

Pre/post-build hooks generate embedded UI, check partitions/invariants and inspect ELF stack/image budget. Report the exact revision/artifact checked.

To exercise the production effect renderer and catalogue API on a host, run from the repository root:

```powershell
python LumigridLedNode/scripts/test_effects.py
python LumigridLedNode/scripts/test_effect_catalog_api.py
```

The renderer harness requires Python and a C++17 compiler (`g++` by default, or the compiler named by `CXX`). AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default; `--no-sanitize` is available for hosts without those runtimes. It builds the production renderer and catalogue with a minimal GPIO enum stub. The API harness additionally requires a host C compiler (`cc` by default, or `CC`) and the repository's managed cJSON sources. It exercises catalogue responses, palette-ID validation, bounded JSON allocation, and error handling on the host. Host results are separate from a full ESP-IDF/PlatformIO build and physical RGB/RGBW/PWM output validation. Report which commands and sanitizer mode were actually run.

## Shared content

From repository root:

```powershell
python scripts/sync_documentation.py
python scripts/sync_documentation.py --check
python scripts/check_api_surface.py --strict
```

The sync script generates the route inventory, capability facts and web docs bundle. Commit guides/generated data together; do not edit generated sections. Public claims need an implementation, enabling condition and limit. Flags are not enforcement proof, intervals are not timing measurements, and previews are not physical evidence.

Edit embedded UI in `embedded/dashboard.html`, not generated C++. Managed-component READMEs/licenses remain upstream-owned.

