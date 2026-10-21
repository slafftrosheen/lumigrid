# Getting started

The target is `esp32dev` with **8 MB flash**. Verify the actual board and wiring against [LED node](LED_NODE.md). Begin commissioning with outputs disabled and a small known load.

## Build

Install web dependencies first: firmware builds use the embedded asset generator.

```powershell
cd lumigrid-lander-main
npm ci
npm run build:embedded
cd ../LumigridLedNode
pio run -e factory
```

`platformio.ini` pins `espressif32@6.7.0`, with ESP-IDF supplied by that platform. Serial defaults are COM3 and 115200 baud; select the actual port. Read [flash layout](FLASH_LAYOUT.md) before updating an existing device: application-only OTA cannot migrate a partition table.

For a board already on the current layout:

```powershell
pio run -e factory -t upload --upload-port COM_PORT
pio device monitor --port COM_PORT --baud 115200
```

Replace `COM_PORT`. These commands write hardware; they are instructions, not evidence of a completed flash.

## First access

1. Read the serial setup banner: `LumiGrid-Setup`, its generated Wi-Fi key and `http://192.168.4.1`.
2. Join that AP with the displayed key. There is no universal production Wi-Fi password or Admin key.
3. Open the device page and create a permanent Admin Access Key of 16–64 characters. Store it privately.
4. Configure channels, pixel counts/order, load ratings and supply assignments. Inspect validation results.
5. Test one channel at low brightness, then save a scene. Enable only needed protocols.
6. If joining a LAN, choose station/fallback mode, follow apply status and reconnect at the reported address.

Factory-first-boot planning enables API/mDNS while lighting, scheduler and protocols remain off. Provisioning and configured operation are distinct states.

## Daily access

Use the controller's device-hosted page on its network. The external web dashboard is at `/dashboard`. An HTTPS-hosted dashboard may be blocked from contacting the node's HTTP/WS endpoints; open the node page directly when blocked.

If access fails, check power, serial diagnostics, active Wi-Fi and `/api/v1/auth/status`. A provisioned node requires its configured key. Read [Access](ACCESS.md) before using physical recovery.

Sources: `LumigridLedNode/platformio.ini`, `main/LumigridRuntime.cpp`, `components/auth/AuthManager.cpp`, `components/factory/include/RuntimeServices.hpp`; web `src/lib/api/transport.ts`.
