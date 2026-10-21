# Dashboard and public site

The controller serves its embedded dashboard at `/`, `/index.html`, `/dashboard`. Edit `lumigrid-lander-main/embedded/dashboard.html`; `npm run build:embedded` and the firmware pre-build hook generate compressed C++ assets. It is not the compiled Svelte dashboard.

## External dashboard

Svelte routes cover channels, live control, scenes, timeline, cues, schedule, sync, firmware, factory, certification, diagnostics, thermal, uptime, preview and visualizer. A route's existence does not establish a hardware sensor. Read connected-node capabilities and runtime state.

The current thermal page explicitly reports that a temperature sensor is unavailable on this hardware/configuration. It is not a functioning temperature-monitoring claim.

Select/login to a node, configure channels, save a scene and inspect actual output status. Accepted, applied and failed command states are distinct. Preview pixels are UI representations, not electrical/photometric measurements.

API calls belong in `src/lib/api/`. `nodeStore.ts` owns WS/snapshots; `commandTracker.ts` reconciles commands; `src/lib/auth/storage.ts` contains storage access. Cue/timeline editors separate documents from playback state.

The storage boundary is not fully consolidated: `auth/nodeAuth.ts` also uses session storage and `fleet/registry.ts` accesses local storage. The strict cross-project audit reports these existing exceptions. Routes themselves do not directly access storage.

Firmware uses HTTP/WS; `transport.ts` constructs those schemes and reports blocked HTTPS-to-local requests. Open the device page when an HTTPS browser context blocks direct control.

## Public content

The landing page describes implemented workflows and bounded capacities. It does not promise installation contracts, testimonials, flicker certification, sync accuracy, environmental ratings or a firmware release. Documentation is built from root guides, with historical material labeled separately.

Sources: web `src/routes/dashboard/`, `src/lib/api/transport.ts`, `src/lib/state/`, `src/lib/auth/`, `embedded/dashboard.html`; firmware `FactoryApi::serve_dashboard`.
