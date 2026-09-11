// ── Options page: always open at the top ────────────────────────────────────
// The pin and connection lists render asynchronously, so Chrome's scroll restoration
// lands against whatever height the document happens to have — intermittently mid-page,
// with the logo scrolled off. An options page has nothing worth restoring.
if (typeof history !== 'undefined' && 'scrollRestoration' in history) history.scrollRestoration = 'manual';
if (typeof window !== 'undefined' && window.scrollTo) {
  window.scrollTo(0, 0);
  window.addEventListener('load', () => window.scrollTo(0, 0));
}
