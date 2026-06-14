// ── PWA: service-worker registration ────────────────────────────
// Registers sw.js (offline shell + runtime cache) so the app is installable
// and works offline. Best-effort: any failure (unsupported, file://, blocked)
// leaves the app running exactly as before. The install *prompt* UI lives in
// the <stencil-install> component; this only wires up the worker.
export const registerServiceWorker = () => {
  if (!('serviceWorker' in navigator)) return;
  // Scope is the directory of sw.js (app root), so it controls the whole app.
  navigator.serviceWorker.register('sw.js').catch(err => {
    console.info('[stencil] service worker registration skipped:', err?.message ?? err);
  });
};
