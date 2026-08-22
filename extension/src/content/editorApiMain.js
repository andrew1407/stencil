// ── Editor-page stencil.extension (MAIN world) ──────────────────────────────
// Injected into the configured editor origin only, while options → "Editor page API" is on.
// Defines `window.__stencilExt`, which the editor's frozen facade surfaces as
// `stencil.extension`: the console gets what the panel's editor mode has. MAIN has no chrome.*,
// so every call is postMessage'd to the ISOLATED bridge and answered id-correlated — unlike the
// page API's one-way sends, each method resolves with the answer or rejects with its error.
(() => {
  // Never re-inject (a second inject would bind a second message listener), and never touch
  // window.stencil — that facade belongs to the editor page, which reads us from __stencilExt.
  if (window.__stencilExt) return;

  // mirror of lib/messages.js (MAIN-world script — can't import)
  const MSG = { EDITOR_LIST: 'stencil-editor-list', EDITOR_STATE: 'stencil-editor-state', EDITOR_IMPORT: 'stencil-editor-import', EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', EDITOR_FOCUS_TAB: 'stencil-editor-focus-tab', SOURCE_TABS: 'stencil-source-tabs', SCAN_TAB: 'stencil-scan-tab', PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop' };
  const SRC = { EXT_API: 'stencil-ext-api', EXT_API_RES: 'stencil-ext-api-res' };

  // End-to-end deadline so a console `await` can't hang when nobody is listening. It must stay
  // strictly LONGER than the 1500 ms legs it wraps (bridge→page, worker→tab) or it would report
  // "no answer" for answers that do arrive — hence two tiers: single-hop, and fan-out/fetch.
  const CALL_TIMEOUT_MS = 4000;
  const SLOW_CALL_TIMEOUT_MS = 30000;
  const SILENT = 'the Stencil extension did not answer';

  // Hard-guard: writing a method / read-only field THROWS instead of silently no-opping. The
  // page API's proxy, on the facade and every entry — `stencil.extension.open = 0` is rejected.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // The calls that stack waits: list (every editor tab), scan (every frame), import (state
  // query then a network fetch). The rest are one hop.
  const SLOW_CALLS = [MSG.EDITOR_LIST, MSG.SCAN_TAB, MSG.EDITOR_IMPORT];

  const pending = new Map();   // call id → settle fn; deleted on the first reply, so a late
                               // answer after a timeout can never re-enter a settled promise.
  let seq = 0;

  window.addEventListener('message', (e) => {
    if (e.source !== window) return;              // same-document bridge → API only
    const d = e.data;
    if (!d || d.source !== SRC.EXT_API_RES || d.id == null) return;
    const settle = pending.get(d.id);
    if (settle) settle(d);
  });

  // One call to the extension: resolves with the worker's response, or REJECTS with its error
  // text, so a console `await` shows a real message instead of an `{ok:false}` to unpack.
  const call = (message) => new Promise((resolve, reject) => {
    const id = `ext-api-${++seq}-${Date.now()}`;
    const settle = (d) => {
      clearTimeout(timer);
      pending.delete(id);
      if (d.ok) { resolve(d.result || {}); return; }
      const err = new Error(d.error || SILENT);
      // The occupied-editor refusal carries the chooser's context; keep it on the error so a
      // script can re-issue open() with an explicit mode instead of re-querying the state.
      if (d.result && d.result.needsChoice) { err.needsChoice = true; err.state = d.result.state || null; }
      reject(err);
    };
    const timer = setTimeout(() => settle({ ok: false, error: SILENT }),
      SLOW_CALLS.includes(message && message.type) ? SLOW_CALL_TIMEOUT_MS : CALL_TIMEOUT_MS);
    pending.set(id, settle);
    window.postMessage({ source: SRC.EXT_API, id, message }, '*');
  });

  // Fire-and-forget hand-offs (new tab / quick-crop) reuse the page API's one-way handlers;
  // posting without an id tells the bridge to relay and not wait for an answer.
  const send = (message) => window.postMessage({ source: SRC.EXT_API, message }, '*');

  // Inline mirror of lib/stencil.js filenameFromUrl — a reasonable project name from a URL.
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

  // The last images() result, so open(0) / crop(1) can address a row by position the way the
  // page API's stencil.items indices do. Replaced wholesale by every scan.
  let lastImages = [];

  // One scanned row in the shape the popup lists, plus `tabId`/`resource` so an import keeps
  // the image's provenance without the caller passing the page URL back in.
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
    // Page-furniture image (favicon / og: <meta> / manifest icon), same flag the popup filters on.
    meta: !!item.meta,
    get name() { return nameFromUrl(this.src || this.videoUrl, this.kind === 'video' ? 'video' : 'image'); },
    open(opts) { return api.open(this, opts); },
    crop(opts) { return api.crop(this, opts); },
  });

  // Validate a target (scanned entry | images() index | URL) → `{ image, resource }`, where
  // `image` is the row the service worker hands to buildHandoff. Throws an explicit Error when
  // nothing is addressable, exactly as the page API's resolveTarget does.
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
      // Provenance is the media URL for a video, the image URL otherwise — mirrors
      // lib/imageModel.js sourceOf, which the SW re-applies when it fetches the bytes.
      const source = kind === 'video' ? (videoUrl || src) : src;
      if (!source) throw new Error('Stencil: this entry has no image URL to open');
      return {
        image: { name: target.name || nameFromUrl(source, kind === 'video' ? 'video' : 'image'), kind, src, videoUrl, source },
        resource: target.resource || '',
      };
    }
    throw new Error('Stencil: pass a scanned image entry, an images() index, or an image URL');
  };

  // A tabId is a number everywhere in chrome's API; catching it here gives a console message
  // instead of the SW's terser 'no tab'.
  const tabIdOf = (tabId) => {
    if (typeof tabId !== 'number') throw new Error('Stencil: pass the tabId of an open tab (see stencil.extension.tabs())');
    return tabId;
  };
  // Omitted tabId = "the tab this console is in": the SW fills it from the sender, so we must
  // send no key at all rather than an explicit undefined.
  const forTab = (tabId) => (tabId == null ? {} : { tabId: tabIdOf(tabId) });

  const api = {
    __stencil: 'editor',
    // Every open editor tab (all windows) with the state its page reported: project name and
    // id, whether it holds an image, a live canvas thumbnail, and which row is THIS tab
    // (`current`). opts.thumbnails = false skips the canvas capture (a cheap poll refresh).
    editors(opts = {}) {
      return call({ type: MSG.EDITOR_LIST, thumbnails: opts.thumbnails !== false }).then((r) => r.editors || []);
    },
    // This tab's own state — the one call the bridge answers itself, without waking the SW.
    get current() {
      return call({ type: MSG.EDITOR_STATE }).then((r) => r.state || null);
    },
    // Focus a tab and raise its window (any tab, not just an editor's — the source-tab picker
    // uses it too). → `{ tabId, windowId }`.
    focus(tabId) {
      return call({ type: MSG.EDITOR_FOCUS_TAB, tabId: tabIdOf(tabId) }).then((r) => ({ tabId: r.tabId, windowId: r.windowId }));
    },
    // Switch an editor tab to another of its OWN projects (default: this tab). An id that tab
    // doesn't have is refused by the page rather than clearing the editor. → `{ projectId, projectName }`.
    switchProject(projectId, opts = {}) {
      return call({ type: MSG.EDITOR_SWITCH_PROJECT, projectId: String(projectId == null ? '' : projectId), ...forTab(opts.tabId) })
        .then((r) => ({ projectId: r.projectId, projectName: r.projectName }));
    },
    // The other open http(s) pages an image can be pulled from (never an editor tab, never a
    // page chrome refuses to inject into). → `[{ tabId, title, url, host, label }]`.
    tabs(opts = {}) {
      return call({ type: MSG.SOURCE_TABS, currentWindowOnly: !!opts.currentWindowOnly }).then((r) => r.tabs || []);
    },
    // Scan another tab for images — the popup's own scanner, on a page you're not standing on.
    // The result is remembered, so open(index) / crop(index) can address a row by position.
    images(tabId, opts = {}) {
      return call({ type: MSG.SCAN_TAB, tabId: tabIdOf(tabId), ...(opts.limit != null ? { limit: opts.limit } : {}) }).then((r) => {
        lastImages = (r.images || []).map((item) => makeEntry(item, r.tabId != null ? r.tabId : tabId, r.url || ''));
        return lastImages;
      });
    },
    // Import an image INTO an editor tab — THIS one unless opts.tabId says otherwise, with no
    // new tab and no navigation. opts.mode: 'ask' (default — refuses, with the chooser's state
    // on the error, when that editor already holds an image), 'new' (a fresh project),
    // 'replace' (swap the image), 'replace-keep' (swap it, keep the annotations).
    // opts also takes { page, crop, incognito, resource }. → `{ tabId, mode, projectId, projectName }`.
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
    // Open a target in a NEW editor tab (the popup's ordinary `#stencil=` hand-off) instead of
    // importing here. One-way like the page API's open(), so it returns the facade for chaining
    // rather than a promise — there is no answer to wait for.
    openInNewTab(target, opts = {}) {
      const t = resolveTarget(target);
      send({ type: MSG.PAGE_OPEN, url: t.image.source, name: t.image.name, source: t.image.source, resource: opts.resource || t.resource || '', incognito: !!opts.incognito, newTab: true });
      return this;
    },
    // Open a target in the quick-crop tool (opts: { album }). One-way, like openInNewTab.
    crop(target, opts = {}) {
      const t = resolveTarget(target);
      send({ type: MSG.PAGE_CROP, url: t.image.source, source: t.image.source, resource: opts.resource || t.resource || '', album: !!opts.album });
      return this;
    },
  };

  // Hide every member from enumeration so the console shows a clean object (no __stencil tag /
  // method dump on console.log or Object.keys); access and DevTools autocomplete still work.
  // Must run before guard()'s freeze, which locks the descriptors.
  for (const k of Reflect.ownKeys(api)) {
    const d = Object.getOwnPropertyDescriptor(api, k);
    if (d.enumerable) Object.defineProperty(api, k, { ...d, enumerable: false });
  }
  const guarded = guard(api);

  // Lock the binding against plain reassignment (writable:false); configurable:true so a
  // reloaded extension can replace it. The editor's facade reads this slot lazily, so it picks
  // up whatever is here at call time.
  try {
    Object.defineProperty(window, '__stencilExt', { value: guarded, writable: false, configurable: true, enumerable: false });
  } catch { /* a non-configurable slot already exists — leave it */ }
})();
