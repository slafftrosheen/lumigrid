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

## Missing for milestone closure

- Bind MQTT client with retained topics and `availability` LWT.
- Complete REST endpoints: `GET/PUT/PATCH /api/v1/config`, `/api/v1/status`.
- Hardware test endpoints with auth gate and 60s hard cap.
- Build guard checks:
  - OE stays disabled until PCA probe + all-off commit succeed.
  - Invalid config reject hysteresis with rolling restart.
  - Reboot during Art-Net/MQTT does not auto-enable outputs.

## Risks / Next steps

1. Auth token is hardcoded — must be provisioned from NVS before network exposure.
2. No gamma correction applied — `tools/generate_gamma.py` generates LUT but not yet integrated.
3. WebSocket handler is broadcast-only; no inbound command parsing.
4. Next PR: Art-Net staging + 10ms coalesce + `pixel_bus.commit(); pwm_bus.commit();`.
