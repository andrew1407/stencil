// Registers sw.js (offline shell + runtime cache), best-effort: any failure leaves the app
// running as before. A newer stamped deploy waits until the user takes it: the toast asks the
// waiting worker to take over, and the page reloads once it has, so no page runs two deploys.
import { notify } from './utils.js';

const offerUpdate = (worker) => {
  notify('A new version of Stencil is ready — click to reload', 'ok', {
    onClick: () => worker.postMessage({ type: 'SKIP_WAITING' }),
  });
};

export const registerServiceWorker = () => {
  if (!('serviceWorker' in navigator)) return;
  const sw = navigator.serviceWorker;
  // A first install takes control without a reload; only a takeover of a controlled page reloads it.
  const hadController = !!sw.controller;
  let reloading = false;
  sw.addEventListener('controllerchange', () => {
    if (!hadController || reloading) return;
    reloading = true;
    location.reload();
  });
  sw.register('sw.js').then((reg) => {
    if (reg.waiting && sw.controller) offerUpdate(reg.waiting);
    reg.addEventListener('updatefound', () => {
      const next = reg.installing;
      next?.addEventListener('statechange', () => {
        if (next.state === 'installed' && sw.controller) offerUpdate(next);
      });
    });
  }).catch(err => {
    console.info('[stencil] service worker registration skipped:', err?.message ?? err);
  });
};
