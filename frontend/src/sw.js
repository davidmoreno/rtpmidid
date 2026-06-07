// Minimal service worker — enables PWA install prompt in Chrome/Edge.
// The app requires an active WebSocket connection to the daemon, so offline
// caching isn't useful here; this just lets the browser install it.
self.addEventListener("install", () => {
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

// Stale-while-revalidate for navigation: always try network first,
// fall back to a cached shell if offline (shows connection banner).
self.addEventListener("fetch", (event) => {
  event.respondWith(
    caches.match(event.request).then(
      (cached) => fetch(event.request).catch(() => cached || Response.error())
    )
  );
});
