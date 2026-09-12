// The lists render asynchronously, so Chrome's scroll restoration lands against whatever
// height the document has — intermittently mid-page. Nothing here is worth restoring.
if (typeof history !== 'undefined' && 'scrollRestoration' in history) history.scrollRestoration = 'manual';
if (typeof window !== 'undefined' && window.scrollTo) {
  window.scrollTo(0, 0);
  window.addEventListener('load', () => window.scrollTo(0, 0));
}
