// Registers sw.js (offline shell + runtime cache), best-effort: any failure leaves the app
// running as before.
export const registerServiceWorker = () => {
  if (!('serviceWorker' in navigator)) return;
  navigator.serviceWorker.register('sw.js').catch(err => {
    console.info('[stencil] service worker registration skipped:', err?.message ?? err);
  });
};
