# LumiGrid

Local lighting control for an ESP32 LED node, with a device-hosted interface and a SvelteKit dashboard.

The firmware implements eight addressable lanes (WS2812 RGB or SK6812 RGBW), eight independent PCA9685 PWM channels, saved scenes, timelines, cue lists, Art-Net/sACN input and optional node synchronization. These are implemented capabilities, not a claim that every hardware combination has been bench-certified.

- [First-time setup](docs/GETTING_STARTED.md)
- [Documentation index](docs/README.md)
- [Hardware and channel mapping](docs/LED_NODE.md)
- [API reference](docs/API_REFERENCE.md)
- [Source-backed audit](docs/SOURCE_OF_TRUTH.md)
- [Build and contribute](docs/CONTRIBUTING.md)
- [Firmware candidate 1.0.0-rc2 and bench acceptance](docs/FIRMWARE_RELEASE_CANDIDATE.md)
- [Hardware, playback, sync and UI polish — 2026-11-01](docs/qualification/2026-11-01-POLISH.md)

## Two projects, one repository

| Path | Responsibility |
| --- | --- |
| `LumigridLedNode/` | ESP-IDF firmware, PlatformIO environments and hardware validation scripts |
| `lumigrid-lander-main/` | Public site, docs reader, operator dashboard and embedded HTML source |
| `docs/` | Shared current guides and historical evidence |
| `design/` | Shared visual tokens and component guidance |
| `scripts/` | Cross-project checks and documentation synchronization |

The firmware generates its compressed dashboard from `lumigrid-lander-main/embedded/dashboard.html`. This is separate from the SvelteKit dashboard.

From the web project run `npm ci`, then `npm run dev` (Node.js 20+). Run `python scripts/sync_documentation.py` from the repository root after changing firmware routes, limits or guides; add `--check` to detect drift. Web dev/build commands refresh the same bundle.

Historical reports under `docs/history/2026-10-21/` apply only to their recorded revisions. Source inspection, host tests, browser checks and physical measurements are distinct evidence.
