// ── Editor bridge content script ────────────────────────────────────────────
// Injected ONLY into the configured Stencil editor origin (registered in
// background.js from the editorUrl setting). Unlike the rest of the extension it is
// SAME-ORIGIN with the editor, so it can read the editor's project registry from
// localStorage — the registry the popup/service-worker can't reach across origins.
// It reports that registry to the service worker, which prunes the opened-images
// ledger for projects the user has deleted (background.js → pruneLedger).
//
// This is READ-ONLY on the editor: it never writes the registry or any project, so it
// cannot create, duplicate, or renumber projects. It only lets the extension drop
// stale "opened" badges — the editor app stays the sole source of truth.
//
// Self-contained (no imports); the guard stops double injection from redeclaring its
// bindings (a top-level `const` would throw on the second inject) or binding the
// `storage`/`stencil:registry-changed` listeners twice.
(() => {
  if (window.__stencilEditorBridge) return;
  window.__stencilEditorBridge = true;

  // mirror of lib/messages.js (classic content script — can't import)
  const MSG = { REGISTRY: 'stencil-registry' };
  // Must match ProjectsStore.REGISTRY_KEY in browser/js/core/projectsStore.js.
  const REGISTRY_KEY = 'stencil_projects_v1';

  const publishRegistry = () => {
    let projects = [];
    try {
      const raw = localStorage.getItem(REGISTRY_KEY);
      const arr = raw ? JSON.parse(raw) : [];
      if (Array.isArray(arr)) {
        // Only the fields reconciliation needs; never the image payload.
        projects = arr
          .filter(m => m && m.id != null)
          .map(m => ({ source: m.source || '', name: m.name || '' }));
      }
    } catch {
      // Unreadable / not JSON → report nothing rather than prune against a bad read.
      return;
    }
    try {
      chrome.runtime.sendMessage({ type: MSG.REGISTRY, projects });
    } catch {
      /* worker asleep / extension context invalidated → a later event retries */
    }
  };

  // Report once on load, then on every registry change. The editing tab fires
  // `stencil:registry-changed` (TabsCoordinator.projectsChanged); the `storage` event
  // covers OTHER editor tabs (localStorage is per-origin shared, and `storage` fires in
  // every same-origin document except the one that wrote it).
  publishRegistry();
  window.addEventListener('stencil:registry-changed', publishRegistry);
  window.addEventListener('storage', (e) => {
    if (!e || e.key == null || e.key === REGISTRY_KEY) publishRegistry();
  });
})();
