// stencil.extension for the editor page's console: MAIN world on the configured editor origin,
// while options → "Editor page API" is on. MAIN has no chrome.*, so every call is postMessage'd
// to the ISOLATED bridge and answered id-correlated. Classic script — no import.
(() => {
  // Never re-inject (a second listener), and never touch window.stencil — the editor page's
  // facade reads us from __stencilExt.
  if (window.__stencilExt) return;

  // mirror of lib/messages.js (MAIN-world script — can't import)
  const MSG = { EDITOR_LIST: 'stencil-editor-list', EDITOR_STATE: 'stencil-editor-state', EDITOR_IMPORT: 'stencil-editor-import', EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', EDITOR_FOCUS_TAB: 'stencil-editor-focus-tab', SOURCE_TABS: 'stencil-source-tabs', SCAN_TAB: 'stencil-scan-tab', PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop' };
  const SRC = { EXT_API: 'stencil-ext-api', EXT_API_RES: 'stencil-ext-api-res' };

  // Must stay strictly LONGER than the 1500 ms legs it wraps (bridge→page, worker→tab), or it
  // reports "no answer" for answers that do arrive — hence two tiers.
  const CALL_TIMEOUT_MS = 4000;
  const SLOW_CALL_TIMEOUT_MS = 30_000;
  const SILENT = 'the Stencil extension did not answer';

  // Hard-guard: writing a method / read-only field THROWS — the page API's proxy.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // Stacked waits: list (every tab), scan (every frame), import (state query then a fetch).
  const SLOW_CALLS = [MSG.EDITOR_LIST, MSG.SCAN_TAB, MSG.EDITOR_IMPORT];

  const pending = new Map();   // call id → settle fn; deleted on the first reply, so a late answer is dropped
  let seq = 0;

  window.addEventListener('message', (e) => {
    if (e.source !== window) return;              // same-document bridge → API only
    const d = e.data;
    if (!d || d.source !== SRC.EXT_API_RES || d.id == null) return;
    const settle = pending.get(d.id);
    if (settle) settle(d);
  });

  // REJECTS with the worker's error text, so a console `await` shows a real message.
  const call = (message) => new Promise((resolve, reject) => {
    const id = `ext-api-${++seq}-${Date.now()}`;
    const settle = (d) => {
      clearTimeout(timer);
      pending.delete(id);
      if (d.ok) { resolve(d.result || {}); return; }
      const err = new Error(d.error || SILENT);
      // The occupied-editor refusal keeps the chooser's context so a script can re-issue open() with a mode.
      if (d.result && d.result.needsChoice) { err.needsChoice = true; err.state = d.result.state || null; }
      reject(err);
    };
    const timer = setTimeout(() => settle({ ok: false, error: SILENT }),
      SLOW_CALLS.includes(message && message.type) ? SLOW_CALL_TIMEOUT_MS : CALL_TIMEOUT_MS);
    pending.set(id, settle);
    window.postMessage({ source: SRC.EXT_API, id, message }, '*');
  });

  // No id = the bridge relays and waits for nothing (the page API's one-way handlers).
  const send = (message) => window.postMessage({ source: SRC.EXT_API, message }, '*');

  // Inline mirror of lib/stencil.js filenameFromUrl.
  const nameFromUrl = (url, fallback = 'image') => {
    const s = String(url || '');
    try {
      if (s.startsWith('data:')) {
        const mime = /^data:([^;,]+)/.exec(s);
        const ext = mime ? (mime[1].split('/')[1] || 'png').replace('+xml', '') : 'png';
        return `${fallback}.${ext}`;
      }
      const u = new URL(s);
      const base = decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || '');
      if (base && /\.[a-z0-9]{2,4}$/i.test(base)) return base;
      return `${base || fallback}.png`;
    } catch { return `${fallback}.png`; }
  };

  // The last images() result, so open(0) / crop(1) address a row by position.
  let lastImages = [];

  // `tabId`/`resource` keep the image's provenance for an import.
  const makeEntry = (item, tabId, resource) => guard({
    __stencilExtEntry: true,
    tabId,
    resource,
    kind: item.kind || 'img',
    src: item.src || '',
    videoUrl: item.videoUrl || '',
    posterUrl: item.posterUrl || '',
    w: item.w || 0,
    h: item.h || 0,
    poster: !!item.poster,
    meta: !!item.meta,
    get name() { return nameFromUrl(this.src || this.videoUrl, this.kind === 'video' ? 'video' : 'image'); },
    open(opts) { return api.open(this, opts); },
    crop(opts) { return api.crop(this, opts); },
  });

  // Throws when nothing is addressable, as the page API's resolveTarget does.
  const resolveTarget = (target) => {
    if (typeof target === 'number') {
      const entry = lastImages[target];
      if (!entry) throw new Error(`Stencil: no image at index ${target} — call stencil.extension.images(tabId) first`);
      return resolveTarget(entry);
    }
    if (typeof target === 'string' && target) {
      return { image: { name: nameFromUrl(target), kind: 'img', src: target, source: target }, resource: '' };
    }
    if (target && (target.__stencilExtEntry || target.src || target.videoUrl)) {
      const kind = target.kind || 'img';
      const src = target.src || '';
      const videoUrl = target.videoUrl || '';
      // Provenance is the media URL for a video (lib/imageModel.js sourceOf).
      const source = kind === 'video' ? (videoUrl || src) : src;
      if (!source) throw new Error('Stencil: this entry has no image URL to open');
      return {
        image: { name: target.name || nameFromUrl(source, kind === 'video' ? 'video' : 'image'), kind, src, videoUrl, source },
        resource: target.resource || '',
      };
    }
    throw new Error('Stencil: pass a scanned image entry, an images() index, or an image URL');
  };

  // A console message instead of the SW's terser 'no tab'.
  const tabIdOf = (tabId) => {
    if (typeof tabId !== 'number') throw new Error('Stencil: pass the tabId of an open tab (see stencil.extension.tabs())');
    return tabId;
  };
  // Omitted tabId = the sender's tab: send no key at all, not an explicit undefined.
  const forTab = (tabId) => (tabId == null ? {} : { tabId: tabIdOf(tabId) });

  const api = {
    __stencil: 'editor',
    // opts.thumbnails = false skips the canvas capture (a cheap poll refresh).
    editors(opts = {}) {
      return call({ type: MSG.EDITOR_LIST, thumbnails: opts.thumbnails !== false }).then((r) => r.editors || []);
    },
    // This tab's own state — the one call the bridge answers itself, without waking the SW.
    get current() {
      return call({ type: MSG.EDITOR_STATE }).then((r) => r.state || null);
    },
    // Any tab, not just an editor's — the source-tab picker uses it too. → { tabId, windowId }
    focus(tabId) {
      return call({ type: MSG.EDITOR_FOCUS_TAB, tabId: tabIdOf(tabId) }).then((r) => ({ tabId: r.tabId, windowId: r.windowId }));
    },
    // A project id that tab doesn't have is refused by the page rather than clearing the editor.
    switchProject(projectId, opts = {}) {
      return call({ type: MSG.EDITOR_SWITCH_PROJECT, projectId: String(projectId == null ? '' : projectId), ...forTab(opts.tabId) })
        .then((r) => ({ projectId: r.projectId, projectName: r.projectName }));
    },
    // → [{ tabId, title, url, host, label }]; never an editor tab or a page chrome refuses to inject into.
    tabs(opts = {}) {
      return call({ type: MSG.SOURCE_TABS, currentWindowOnly: !!opts.currentWindowOnly }).then((r) => r.tabs || []);
    },
    // Remembered, so open(index) / crop(index) can address a row by position.
    images(tabId, opts = {}) {
      return call({ type: MSG.SCAN_TAB, tabId: tabIdOf(tabId), ...(opts.limit != null ? { limit: opts.limit } : {}) }).then((r) => {
        lastImages = (r.images || []).map((item) => makeEntry(item, r.tabId != null ? r.tabId : tabId, r.url || ''));
        return lastImages;
      });
    },
    // opts.mode: 'ask' (default — refuses, with the chooser's state on the error, when the editor
    // holds an image) | 'new' | 'replace' | 'replace-keep'; also { page, crop, incognito, resource }.
    open(target, opts = {}) {
      const t = resolveTarget(target);
      return call({
        type: MSG.EDITOR_IMPORT,
        ...forTab(opts.tabId),
        image: t.image,
        resource: opts.resource || t.resource || '',
        mode: opts.mode || 'ask',
        incognito: !!opts.incognito,
        ...(opts.page ? { page: opts.page } : {}),
        ...(opts.crop ? { crop: opts.crop } : {}),
      }).then((r) => ({ tabId: r.tabId, mode: r.mode, projectId: r.projectId, projectName: r.projectName }));
    },
    // One-way like the page API's open(): returns the facade for chaining, not a promise.
    openInNewTab(target, opts = {}) {
      const t = resolveTarget(target);
      send({ type: MSG.PAGE_OPEN, url: t.image.source, name: t.image.name, source: t.image.source, resource: opts.resource || t.resource || '', incognito: !!opts.incognito, newTab: true });
      return this;
    },
    crop(target, opts = {}) {
      const t = resolveTarget(target);
      send({ type: MSG.PAGE_CROP, url: t.image.source, source: t.image.source, resource: opts.resource || t.resource || '', album: !!opts.album });
      return this;
    },
  };

  // Non-enumerable for a clean console.log; must run before guard()'s freeze locks the descriptors.
  for (const k of Reflect.ownKeys(api)) {
    const d = Object.getOwnPropertyDescriptor(api, k);
    if (d.enumerable) Object.defineProperty(api, k, { ...d, enumerable: false });
  }
  const guarded = guard(api);

  // configurable:true so a reloaded extension can replace it; the editor's facade reads this slot lazily.
  try {
    Object.defineProperty(window, '__stencilExt', { value: guarded, writable: false, configurable: true, enumerable: false });
  } catch { /* a non-configurable slot already exists — leave it */ }
})();
