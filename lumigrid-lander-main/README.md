# LumiGrid web project

SvelteKit LED Node product site, localized customer setup guide, shared engineering reference reader and external control dashboard. The device-hosted interface is authored separately in `embedded/dashboard.html`.

Node.js 20+ is required. Run `npm ci`, `npm run dev`. Use `npm run test:marketing`, `npm run test:unit`, `npm run check`, `npm run build` for validation. Run `npm run test` for browser-based customer journeys. `npm run build:embedded` generates firmware UI assets.

This project expects its parent repository: root docs and firmware feed `scripts/sync_documentation.py`. Dev/build hooks refresh the bundle; `npm run docs:check` detects drift.

| Area | Owner |
| --- | --- |
| Product marketing | src/lib/publicContent.ts and src/routes/+page.svelte |
| Multilingual first-time setup | src/routes/start/+page.svelte, using publicContent.ts |
| Marketing regression checks | scripts/test-public-copy.mjs and tests/e2e/landing.spec.ts |
| Guides | ../docs/ and generated documentation bundle |
| API contracts | src/lib/api/ |
| Live state/WebSocket | src/lib/state/nodeStore.ts |
| Authentication storage | src/lib/auth/storage.ts |
| Embedded UI | embedded/dashboard.html and scripts/build-embedded.mjs |

Firmware serves HTTP/WS; hosted HTTPS browsers may block local access. Use the node page when blocked. See [shared docs](../docs/README.md), [source audit](../docs/SOURCE_OF_TRUTH.md) and [development guide](../docs/CONTRIBUTING.md).

Customer-facing pages must describe LED Node capabilities and installed-light outcomes, not chip models, board internals or build commands. Use root engineering guides for component-specific instructions. The homepage's lighting previews and use-case scenes are illustrations, not product photography or live hardware control. Performance note: the service worker excludes large promotional video and installer packages from precache and never caches authenticated control routes.
