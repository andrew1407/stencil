// ── Editor bridge content script ────────────────────────────────────────────
// Injected ONLY into the configured Stencil editor origin (registered in background.js
// from the editorUrl setting). SAME-ORIGIN with the editor, so it can read the editor's
// project registry from localStorage (unreachable cross-origin from popup/SW). Reports
// that registry to the SW, which prunes the opened-images ledger for deleted projects
// (background.js → pruneLedger).
// READ-ONLY on the editor: never writes the registry or any project (can't create,
// duplicate, or renumber); only lets the extension drop stale "opened" badges — the
// editor app stays the sole source of truth.
// Self-contained (no imports); the guard stops double injection from redeclaring
// bindings (a top-level `const` throws on second inject) or binding the
// `storage`/`stencil:registry-changed` listeners twice.
(() => {
  if (window.__stencilEditorBridge) return;
  window.__stencilEditorBridge = true;

  // mirror of lib/messages.js (classic content script — can't import)
  const MSG = { REGISTRY: 'stencil-registry', PAGE_PIN: 'stencil-page-pin' };
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
  // covers OTHER editor tabs (storage fires in every same-origin document except the
  // one that wrote it).
  publishRegistry();
  window.addEventListener('stencil:registry-changed', publishRegistry);
  window.addEventListener('storage', (e) => {
    if (!e || e.key == null || e.key === REGISTRY_KEY) publishRegistry();
  });

  // Editor → extension UNPIN relay. When a project's image changes, the editor app posts a
  // same-window message; forward it to the SW as a PAGE_PIN with pin:false (which removes the
  // pin via setPinned/removePinEntry). `resource` is required so the SW computes the right
  // (site, source) pin key — the editor passes the OLD image's source/resource.
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;                       // same-document page → bridge only
    const m = e && e.data;
    if (!m || m.source !== 'stencil-editor-bridge' || m.type !== 'unpin') return;
    try {
      chrome.runtime.sendMessage({
        type: MSG.PAGE_PIN, pin: false,
        source: m.pinSource || '', resource: m.resource || '',
        name: m.name || '', kind: m.kind || 'image',
      });
    } catch {
      /* worker asleep / extension context invalidated — a stale pin is harmless */
    }
  });
})();
