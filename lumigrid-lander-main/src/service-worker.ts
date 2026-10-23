/// <reference lib="webworker" />
import { build, files, version } from '$service-worker';

const CACHE_PREFIX = 'lg-v';
const CACHE = `${CACHE_PREFIX}${version}`;
// The marketing site must not pre-download large media or installation
// packages when a visitor opens the product page.
const LARGE_ASSET = /\.(?:mp4|webm|mov|pdf|zip|bin|gz|br|map)$/i;
const STATIC_ASSETS = [...new Set([...build, ...files.filter(path => !LARGE_ASSET.test(path))])];

function isLiveRoute(path: string): boolean {
  return /\/(?:api|dashboard)(?:\/|$)/.test(path);
}

self.addEventListener('install', (event: ExtendableEvent) => {
  event.waitUntil(caches.open(CACHE).then(cache => cache.addAll(STATIC_ASSETS)));
});

self.addEventListener('activate', (event: ExtendableEvent) => {
  event.waitUntil(
    caches.keys().then(keys => Promise.all(
      // Do not remove caches belonging to another app hosted on this origin.
      keys.filter(key => key.startsWith(CACHE_PREFIX) && key !== CACHE).map(key => caches.delete(key))
    ))
  );
});

self.addEventListener('fetch', (event: FetchEvent) => {
  const request = event.request;
  const url = new URL(request.url);
  if (request.method !== 'GET' ||
      url.origin !== location.origin ||
      request.headers.has('range') ||
      LARGE_ASSET.test(url.pathname) ||
      isLiveRoute(url.pathname) ||
      !(request.mode === 'navigate' || ['image', 'style', 'script', 'font'].includes(request.destination))) {
    // Network-only: control sessions, API responses and substantial downloads.
    return;
  }

  event.respondWith((async () => {
    try {
      const response = await fetch(request);
      const directive = response.headers.get('cache-control') || '';
      const privateResponse = /(?:no-store|private)/i.test(directive) ||
        response.headers.has('set-cookie');
      if (response.ok && response.status === 200 && !response.headers.has('content-range') && !privateResponse) {
        const cache = await caches.open(CACHE);
        // Caching is best-effort. An individual storage failure must not
        // prevent the landing page from appearing to the visitor.
        await cache.put(request, response.clone()).catch(() => {});
      }
      return response;
    } catch (error) {
      const cached = await caches.match(request);
      if (cached) return cached;
      if (request.mode === 'navigate') {
        const offline = await caches.match('/offline.html');
        if (offline) return offline;
      }
      throw error;
    }
  })());
});
