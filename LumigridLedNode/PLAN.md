# LumigridLedNode — ARREST/SPEC PLAN

## Implemented now
- `include/config/lg_config.hpp` — exact `NodeConfig`, `PixelLaneConfig`, `PwmGroupConfig`, `ArtnetConfig`, `MqttConfig`, `PixelOrder`, `PwmKind`.
- `include/config/lg_config_store.hpp` + `src/config/lg_config_store.cpp` — `ConfigStore` with NVS namespace `lumigrid`, key `node_config`, factory defaults, full validation.
- `include/protocols/lg_artnet.hpp` + `src/protocols/lg_artnet.cpp` — little-endian safe `parse_packet`, staging envelope, `ArtSync`-able skeleton.
- `include/protocols/protocol_router.hpp` + `src/protocols/protocol_router.cpp` — ownership boundary stub; receives from Art-Net, hands validated config to output.
- `include/output/PixelBus.h/.cpp`, `PwmBus.h/.cpp` updated to `lg::NodeConfig` path.
- `include/diagnostics/hardware_test.hpp/.cpp` — hardware test skeleton with exclusive-output lock.
- `include/api/api_server.hpp/.cpp`, `src/network/ApiServer.*` adapted to new api component.
- `PLAN.md` — this file.

## Missing for milestone closure
- Bind UDP to `6454`, start Art-Net task loop with frame coalescing + staging commit.
- MQTT client wiring with retained topics and `availability` LWT.
- Complete REST endpoints: `GET/PUT/PATCH /api/v1/config`, `/api/v1/status`, `/api/v1/test/*`.
- Hardware test endpoints plus auth gate and 60s hard cap.
- Build guard checks:
  - OE stays disabled until PCA probe + all-off commit succeed.
  - Invalid config reject hysteresis with rolling restart.
  - Reboot during Art-Net/MQTT does not auto-enable outputs.

## Risks / Next steps
1) Old `include/config/ConfigStore.h` is now legacy; remove once all call sites migrate to `lg::ConfigStore`.
2) Don’t treat `api/api_server.hpp` as Arduino-web-server code; real `esp_http_server`/`lwip udp` integration is required.
3) Next suggested PR: Art-Net staging + 10ms coalesce + `pixel_bus.commit(); pwm_bus.commit();`.
