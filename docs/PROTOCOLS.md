# Protocols

## HTTP and live state

The node serves HTTP `/api/v1/` and `/ws`. The capability manifest advertises HTTP API 1 and WebSocket 2. No SSE stream is registered in the current route table. See [API reference](API_REFERENCE.md) rather than assuming old `/json`, `/edit`, `/update` routes.

Browser sessions and single-use WS tickets are described in [Access](ACCESS.md). HTTPS browser contexts may block plain HTTP/WS node access.

## Art-Net and sACN

Both receivers are implemented and off by default. `DmxRouter` maps channels/zones, handles ownership/timeouts and HTP/LTP merging. Defaults: 2000 ms timeout, live layer 2. Addressable patching supports wash/direct pixels; PWM channels are independent dimmers. Validate universe/channel ranges through the config API.

Discovery/subscriptions follow active configuration. Protocol implementation does not certify every console or maximum network load.

## Node sync

`NodeSync` uses UDP 6455, packet version 6, eight peers, nominal 100 ms interval and 50 ms during manual crossfade. Roles: Auto, Master, Worker. Sync requires configuration and a connected station on the shared LAN.

Scene/timeline/cue transport reports missing/mismatched content. Deploy matching content and inspect show manifests/status. Packet intervals are not measured phase error or guaranteed timing. Validate network loss and reconnect behavior physically.

## Discovery

mDNS discovery has 32 node slots, distinct from eight sync peers. Queries run every 10 seconds; sightings are online up to 20 seconds and stale up to 60 seconds. Presence does not grant control authority.

## MQTT

With MQTT enabled and a broker configured, `MqttService` publishes `<prefix>/state`, `<prefix>/event`, retained `<prefix>/availability`, and subscribes to `<prefix>/command`. Availability uses online/offline plus a retained last will. Periodic state is configured at 60 seconds; the runtime supplies prefix and command callback.

The reviewed service does not wire dedicated username/password or certificate provisioning. Do not describe it as a secured cloud fleet service. Validate broker/command integration separately.

Sources: `LumigridLedNode/components/protocols/`, `sync/NodeSync.cpp`, `sync/include/NodeSync.hpp`, `discovery/include/FleetDiscoveryService.hpp`, `network/MqttService.cpp`, and `main/LumigridRuntime.cpp`.
