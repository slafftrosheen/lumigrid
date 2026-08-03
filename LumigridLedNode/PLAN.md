# LumigridLedNode — Factory Firmware

## Architecture

ESP32 (ESP-IDF 5.2, PlatformIO) driving 8× addressable LED lanes + 8× PCA9685 PWM channels.
Factory test mode with REST + WebSocket API, Art-Net DMX streaming, mDNS discovery.

## Component Layout

```
components/
  board/          — Pin definitions (lg_board_pins.hpp), factory constants (FactoryApp.hpp)
  output/         — AddressableLeds (RMT), Pca9685 (I2C), OutputController (mutex-protected)
  factory/        — FactoryRunner (effect loop task), FactoryEffects, FactoryApi (HTTP + WS)
  api/            — ApiAuth (bearer token validation)
  config/         — NVS-backed scenes, timeline, calendar, and device configuration
  signage/        — Persistent scene rendering, effect layers, and power management
  scheduler/      — SNTP-backed calendar automation and beat timeline playback
  sync/           — UDP leader/follower clock discipline and room health
  network/        — WifiService (STA/AP/fallback), MdnsService
  protocols/      — ArtnetDiscovery (UDP Art-Net + ArtPollReply)
main/             — app_main boot sequence, dashboard.html embedded
```

## Implemented

- `OutputController` — mutex-protected facade for AddressableLeds + Pca9685
- `AddressableLeds` — 8-lane WS2812 via ESP-IDF RMT, buffer→show pipeline
- `Pca9685` — I2C driver with mutex-protected bus, 12-bit PWM, error-propagating init
- `FactoryRunner` — FreeRTOS task with command queue, auto-loop, stream, direct-test modes
- `FactoryEffects` — Identity, PixelChase, Rainbow, WhiteWalk, PwmWalk, PwmFade, CombinedPulse, BurnIn
- `FactoryApi` — REST endpoints (`GET /status`, `POST /effect`, `/lane`, `/pwm`, `/abort`, `/auto-loop`), WS broadcast, embedded HTML dashboard
- `ApiAuth` — Bearer token gate (factory hardcoded, TODO: NVS provisioning)
- `WifiService` — AP-only, STA-only, or STA-with-AP-fallback modes
- `MdnsService` — `_http._tcp` advertisement
- `ArtnetDiscovery` — UDP listener on port 6454, ArtPoll→ArtPollReply, ArtDmx→callback
- Consumer dashboard — semantic scene controls, effect metadata, visual timeline, calendar cues,
  and sync topology health
- `Scheduler` — cached timeline playback plus local-time calendar triggers
- `NodeSync` — bounded monotonic clock discipline, packet sequencing, leader election, and peer ACKs

## Missing for milestone closure

- Bind MQTT client with retained topics and `availability` LWT.
- Validate sync timing with two or more ESP32s on representative wired and noisy Wi-Fi networks.
- Hardware test endpoints with auth gate and 60s hard cap.
- Build guard checks:
  - OE stays disabled until PCA probe + all-off commit succeed.
  - Invalid config reject hysteresis with rolling restart.
  - Reboot during Art-Net/MQTT does not auto-enable outputs.

## Risks / Next steps

1. Auth token is hardcoded — must be provisioned from NVS before network exposure.
2. No gamma correction applied — `tools/generate_gamma.py` generates LUT but not yet integrated.
3. WebSocket handler is broadcast-only; no inbound command parsing.
4. UDP sync is intended for trusted local networks; production deployments still need
   measured drift/packet-loss acceptance criteria and a provisioning strategy for sync trust.
5. Next PR: Art-Net staging + 10ms coalesce + `pixel_bus.commit(); pwm_bus.commit();`.
