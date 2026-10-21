# LumiGrid web project

SvelteKit public site, shared documentation reader and external operator dashboard. The device-hosted interface is authored separately in `embedded/dashboard.html`.

Node.js 20+ is required. Run `npm ci`, `npm run dev`. Use `npm run test:unit`, `npm run check`, `npm run build` for validation. `npm run build:embedded` generates firmware UI assets.

This project expects its parent repository: root docs and firmware feed `scripts/sync_documentation.py`. Dev/build hooks refresh the bundle; `npm run docs:check` detects drift.

| Area | Owner |
| --- | --- |
| Public copy | src/lib/publicContent.ts and src/routes/+page.svelte |
| Guides | ../docs/ and generated documentation bundle |
| API contracts | src/lib/api/ |
| Live state/WebSocket | src/lib/state/nodeStore.ts |
| Authentication storage | src/lib/auth/storage.ts |
| Embedded UI | embedded/dashboard.html and scripts/build-embedded.mjs |

Firmware serves HTTP/WS; hosted HTTPS browsers may block local access. Use the node page when blocked. See [shared docs](../docs/README.md), [source audit](../docs/SOURCE_OF_TRUTH.md) and [development guide](../docs/CONTRIBUTING.md).
