// ── window.stencil — console control API for the Stencil editor ─────────────
// A thin, chainable facade over the live DrawingApp. It NEVER reimplements editor
// behaviour: every mutation routes through the same shared core methods the toolbar
// UI uses (setColor/setPageSize/applyCrop/loadImageFromFile/…), so scripting the
// editor from the console and clicking the toolbar stay perfectly in sync.
//
// Encapsulation: built as a closure factory, NOT a class — `app` and all state live
// in this function's scope, so the returned objects carry no fields at all. `stencil`
// previews as just its getters/methods; there is no `_app`/`#app` to read or overwrite
// from page script. (A DevTools user can still reach `app` via a method's [[Scopes]] —
// nothing can hide data from the console — but no property access exposes it.)
//
// Construction: index.js builds this after the app and assigns window.stencil.
// Most methods return the facade (or a Project/Line/Point) for chaining:
//   stencil.apply({ page: 'a3', pointSize: 9 }).rotateLeft().crop({ x1: '10%' })
//   (await stencil.load(url)).crop({ x2: '-10%' }).apply({ lineColor: 'aqua' })
import { hotkeys } from '../core/hotkeys.js';
import { resolveAxisPx } from '../core/units.js';
import { cropAspect } from '../core/cropGeometry.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { ACCENTS, isAccent } from '../core/accents.js';

const str = (v) => (v == null ? '' : String(v));

export const createStencil = (app) => {
  // ── private state (closure-captured; never a property of any returned object) ──
  let peers = [];
  try { app.tabs.onPeers((ids) => { peers = Array.isArray(ids) ? ids : []; }); } catch { /* no coordinator */ }
  const openedIds = () => {
    const ids = new Set(peers.filter((x) => x != null));
    if (app.activeProjectId != null) ids.add(app.activeProjectId);   // own active id, defensively
    return ids;
  };

  let stencil;   // forward ref so wrappers can return the facade for chaining

  // Hard-guard an API object: a property with a real setter still writes through, but
  // writing a method or a read-only getter (or adding/deleting a property) THROWS rather
  // than silently no-opping on a frozen object in the non-strict console. Applied to the
  // facade and every Line / Point / Project / settings object it hands back, so e.g.
  // `stencil.lines[0].move = 3`, `stencil.load = 0`, or `pt.remove = 1` is rejected.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // Dismiss any open editor modal (projects chooser, shortcuts/info, links, visuals, …)
  // so a console-driven load isn't left hidden behind one. Toggles the shared .modal-open
  // class each modal uses; their onClose cleanup is idempotent and re-runs on next open.
  const closeModals = () => {
    try { document.querySelectorAll('.app-modal-overlay.modal-open').forEach((o) => o.classList.remove('modal-open')); }
    catch { /* no DOM (node tests) */ }
  };

  // Normalize any CSS color (named like 'red', rgb()/hsl(), #rgb) to '#rrggbb' so the
  // console accepts the same colors CSS does — the editor's <input type=color> controls
  // only take #rrggbb. 'transparent'/null pass through (fills allow them); a truly
  // unparseable value is returned unchanged (so it still surfaces as an error, not black).
  const colorCanvas = (() => { try { return document.createElement('canvas').getContext('2d'); } catch { return null; } })();
  const toHexColor = (v) => {
    if (v == null) return v;
    const s = String(v).trim();
    if (!s || s.toLowerCase() === 'transparent') return s;
    if (/^#[0-9a-f]{6}$/i.test(s)) return s.toLowerCase();
    if (/^#[0-9a-f]{3}$/i.test(s)) return ('#' + s.slice(1).replace(/./g, (c) => c + c)).toLowerCase();
    if (!colorCanvas) return s;
    // The browser resolves any valid CSS color via fillStyle; probe two bases so an
    // INVALID value (which leaves each base untouched) is detected and left as-is.
    colorCanvas.fillStyle = '#000'; colorCanvas.fillStyle = s; const a = colorCanvas.fillStyle;
    colorCanvas.fillStyle = '#fff'; colorCanvas.fillStyle = s; const b = colorCanvas.fillStyle;
    if (a !== b) return s;                       // unparseable → unchanged
    if (/^#[0-9a-f]{6}$/i.test(a)) return a;
    const m = /^rgba?\(\s*(\d+),\s*(\d+),\s*(\d+)/i.exec(a);   // alpha form → drop alpha
    return m ? '#' + [m[1], m[2], m[3]].map((n) => (+n).toString(16).padStart(2, '0')).join('') : s;
  };

  // ── Point: wraps one {x,y} in a line's points (crop-local px). lineIdx === -1 is
  // the in-progress currentLine (matches the coord table's target resolution). ──
  const makePoint = (lineIdx, ptIdx) => {
    const raw = () => {
      const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
      return line ? line.points[ptIdx] : null;
    };
    let point = {
      get lineIdx() { return lineIdx; },
      get ptIdx() { return ptIdx; },
      get x() { const p = raw(); return p ? p.x : undefined; },
      get y() { const p = raw(); return p ? p.y : undefined; },
      set x(v) { app.setPointCoord(lineIdx, ptIdx, 'x', v); },
      set y(v) { app.setPointCoord(lineIdx, ptIdx, 'y', v); },
      // Absolute set of x/y (and optionally the parent line's marker size).
      apply({ x, y, size } = {}) {
        if (x != null) app.setPointCoord(lineIdx, ptIdx, 'x', x);
        if (y != null) app.setPointCoord(lineIdx, ptIdx, 'y', y);
        if (size != null) {
          const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
          if (line) { line.markerSize = Number(size); app.saveHistory(); app.renderer.redraw(); }
        }
        return point;
      },
      // Relative move by pixels on either axis.
      move({ x, y } = {}) {
        const p = raw();
        if (!p) return point;
        if (x != null) app.setPointCoord(lineIdx, ptIdx, 'x', p.x + Number(x));
        if (y != null) app.setPointCoord(lineIdx, ptIdx, 'y', p.y + Number(y));
        return point;
      },
      // Remove this point; empties the line → the line is dropped too. Returns the
      // owning line (or the facade if the line is gone) so chaining stays useful.
      remove() {
        app.removePoint(lineIdx, ptIdx);
        return (lineIdx === -1 || !app.lines[lineIdx]) ? stencil : makeLine(lineIdx);
      },
    };
    point = guard(point);   // reassign so chained returns hand back the guarded proxy
    return point;
  };

  // ── Line ──
  const makeLine = (startIdx) => {
    let idx = startIdx;                       // may shift when an earlier line is removed
    const obj = () => app.lines[idx];
    const commit = () => { app.saveHistory(); app.renderer.redraw(); };
    const setProp = (prop, value) => { const l = obj(); if (l) { l[prop] = value; commit(); } };
    let line = {
      get idx() { return idx; },
      get points() { const l = obj(); return l ? l.points.map((_, i) => makePoint(idx, i)) : []; },
      get color() { return obj()?.color; }, set color(v) { setProp('color', toHexColor(str(v))); },
      get thickness() { return obj()?.thickness; }, set thickness(v) { setProp('thickness', Number(v)); },
      get markerSize() { return obj()?.markerSize; }, set markerSize(v) { setProp('markerSize', Number(v)); },
      get style() { return obj()?.style; }, set style(v) { setProp('style', str(v)); },
      get fillColor() { return obj()?.fillColor; }, set fillColor(v) { setProp('fillColor', v == null ? 'transparent' : toHexColor(str(v))); },
      // Batch style update. Accepts color/thickness/markerSize|pointSize/style/fillColor.
      apply(opts = {}) {
        const l = obj();
        if (!l) return line;
        if (opts.color != null) l.color = toHexColor(str(opts.color));
        if (opts.thickness != null) l.thickness = Number(opts.thickness);
        const ms = opts.markerSize ?? opts.pointSize;
        if (ms != null) l.markerSize = Number(ms);
        if (opts.style != null) l.style = str(opts.style);
        if (opts.fillColor != null) l.fillColor = opts.fillColor === 'transparent' ? 'transparent' : toHexColor(str(opts.fillColor));
        commit();
        return line;
      },
      // Translate every point by pixel deltas.
      move({ x = 0, y = 0 } = {}) {
        const l = obj();
        if (!l) return line;
        const dx = Number(x) || 0, dy = Number(y) || 0;
        for (const p of l.points) { p.x += dx; p.y += dy; }
        app.saveHistory(); app.renderer.redraw(); app.coordTable.update(l.points, idx);
        return line;
      },
      // Rotate the points by `deg` clockwise around `pivot` ({x,y}) or the bbox centre.
      rotate(deg, pivot) {
        const l = obj();
        if (!l || !l.points.length) return line;
        const cx = pivot?.x ?? (Math.min(...l.points.map(p => p.x)) + Math.max(...l.points.map(p => p.x))) / 2;
        const cy = pivot?.y ?? (Math.min(...l.points.map(p => p.y)) + Math.max(...l.points.map(p => p.y))) / 2;
        const rad = (Number(deg) || 0) * Math.PI / 180, cos = Math.cos(rad), sin = Math.sin(rad);
        for (const p of l.points) {
          const dx = p.x - cx, dy = p.y - cy;
          p.x = cx + dx * cos - dy * sin;
          p.y = cy + dx * sin + dy * cos;
        }
        app.saveHistory(); app.renderer.redraw(); app.coordTable.update(l.points, idx);
        return line;
      },
      // Insert a point. `at.neighbour` ({x,y} or index) + `at.after` choose the slot.
      add(point, at = {}) {
        const l = obj();
        if (!l || !point) return line;
        const pt = { x: Number(point.x) || 0, y: Number(point.y) || 0 };
        let i = l.points.length;
        if (at.neighbour != null) {
          const n = typeof at.neighbour === 'number'
            ? at.neighbour
            : l.points.findIndex(p => p.x === at.neighbour.x && p.y === at.neighbour.y);
          if (n >= 0) i = at.after === false ? n : n + 1;
        }
        l.points.splice(i, 0, pt);
        app.saveHistory(); app.renderer.redraw(); app.coordTable.update(l.points, idx);
        return line;
      },
      // Remove a point by index or by a point reference ({x,y} or a Point wrapper).
      remove(indexOrPoint) {
        const l = obj();
        if (!l) return line;
        let i = indexOrPoint;
        if (typeof indexOrPoint !== 'number') {
          const ref = indexOrPoint && typeof indexOrPoint === 'object' ? { x: indexOrPoint.x, y: indexOrPoint.y } : null;
          i = ref ? l.points.findIndex(p => p.x === ref.x && p.y === ref.y) : -1;
        }
        if (i >= 0) app.removePoint(idx, i);
        return line;
      },
      // Append another line's points to this one and drop the other line.
      join(other) {
        const l = obj();
        const oIdx = other && typeof other.idx === 'number' ? other.idx : -1;
        const o = oIdx >= 0 ? app.lines[oIdx] : null;
        if (!l || !o || oIdx === idx) return line;
        l.points.push(...o.points.map(p => ({ x: p.x, y: p.y })));
        app.removeLine(oIdx);                   // saves history + redraws
        if (oIdx < idx) idx -= 1;               // our index shifts if the other was before us
        return line;
      },
    };
    line = guard(line);   // reassign so chained returns hand back the guarded proxy
    return line;
  };

  // ── Project ──
  const makeProject = (id, incognito = false) => {
    const store = () => app.storage.store;
    const meta = () => (id == null ? null : store().getMeta(id));
    const isActive = () => id != null && id === app.activeProjectId;
    // Update a provenance link, live: active project via app state + save; a stored
    // (possibly open-in-another-tab) project via the registry + a broadcast.
    const setLink = (metaKey, appKey, v) => {
      const val = str(v).trim() || null;
      if (incognito) throw new Error('Cannot set links on an incognito editor');
      if (isActive()) {
        app[appKey] = val;
        app.storage.save();
        // Refresh any open links modal immediately (save's broadcast is debounced ~400ms).
        try { window.dispatchEvent(new Event('stencil:registry-changed')); } catch { /* no DOM */ }
        return;
      }
      const proj = store().get(id);
      if (!proj) throw new Error(`Unknown project ${id}`);
      proj.meta[metaKey] = val;
      proj.payload.layout[appKey] = val;
      store().upsert(proj.meta, proj.payload);
      app.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    };
    let project = {
      get id() { return id; },
      get incognito() { return incognito; },
      get isOpened() { return incognito ? true : openedIds().has(id); },
      get expiresAt() { const m = meta(); return m ? store().expiresAt(m) : null; },
      get isExpired() { const m = meta(); return m ? store().isExpired(m) : false; },
      // { image: { width, height } } (image null when unknown).
      get size() {
        if (isActive() && app.image) return { image: { width: app.image.width, height: app.image.height } };
        const m = meta();
        return { image: (m && m.imageW != null && m.imageH != null) ? { width: m.imageW, height: m.imageH } : null };
      },
      get name() { return incognito ? 'Incognito (unsaved)' : (meta()?.name ?? null); },
      set name(v) {
        if (incognito) throw new Error('Cannot rename an incognito editor');
        const clean = str(v).trim();
        if (!clean) throw new Error('Project name cannot be empty');
        if (store().nameExists(clean, id)) throw new Error(`A project named "${clean}" already exists`);
        if (!app.renameProject(id, clean)) throw new Error(`Could not rename project ${id}`);
      },
      get imageName() {
        if (isActive()) return app.imageBaseName ?? null;
        return store().get(id)?.payload?.layout?.imageBaseName ?? null;
      },
      set imageName(v) {
        if (!isActive()) throw new Error('imageName can only be set on the active project');
        app.imageBaseName = str(v);
        app.storage.save();
      },
      get layout() {
        if (isActive()) app.storage.save();      // flush so the snapshot is current
        return store().get(id)?.payload?.layout ?? undefined;
      },
      get source() { return isActive() ? (app.imageSource ?? null) : (meta()?.source ?? null); },
      set source(v) { setLink('source', 'imageSource', v); },
      get resource() { return isActive() ? (app.imageResource ?? null) : (meta()?.resource ?? null); },
      set resource(v) { setLink('resource', 'imageResource', v); },
      renew() { app.renewProject(id); return project; },
      // Close this project's editor (keeps the saved project). `fully` also closes the
      // browser tab/window if it's the active one here.
      close({ fully = false } = {}) {
        if (incognito || isActive()) app.closeProject(app.activeProjectId, { fully });
        else app.closeProject(id, { fully });
        return project;
      },
      open() { if (id != null) app.switchToProject(id); return project; },
    };
    project = guard(project);   // reassign so chained returns hand back the guarded proxy
    return project;
  };

  const incognitoList = () => (app.storage.incognito ? [makeProject(null, true)] : []);
  const savedOpen = () => {
    const open = openedIds();
    return app.storage.store.list().filter((m) => open.has(m.id)).map((m) => makeProject(m.id));
  };

  // ── Settings namespace (fresh object per access; setters close over app) ──
  const settingsAccessors = () => ({
    get color() { return app.color; }, set color(v) { app.setColor(toHexColor(v)); },
    get lineColor() { return app.color; }, set lineColor(v) { app.setColor(toHexColor(v)); },
    get thickness() { return app.thickness; }, set thickness(v) { app.setThickness(v); },
    get pointSize() { return app.markerSize; }, set pointSize(v) { app.setMarkerSize(v); },
    get markerSize() { return app.markerSize; }, set markerSize(v) { app.setMarkerSize(v); },
    get lineStyle() { return app.style; }, set lineStyle(v) { app.setLineStyle(v); },
    get pointStyle() { return app.showPoints; }, set pointStyle(v) { app.setShowPoints(v); },   // points visible?
    get showPoints() { return app.showPoints; }, set showPoints(v) { app.setShowPoints(v); },
    get showLines() { return app.showLines; }, set showLines(v) { app.setShowLines(v); },
    get filter() { return app.imageFilter; }, set filter(v) { app.setImageFilter(v); },
    get filterColor() { return app.filterColor; }, set filterColor(v) { app.setFilterColor(toHexColor(v)); },
    get unit() { return app.unit; }, set unit(v) { app.setUnit(v); },
    get pageSize() { return app.pageSize; }, set pageSize(v) { app.setPageSize(v); },            // case-insensitive ('a3' ok)
    get pageWidth() { return app.customPageWidth; }, set pageWidth(v) { app.setCustomPageWidth(Number(v)); },     // cm; applies when pageSize='custom'
    get pageHeight() { return app.customPageHeight; }, set pageHeight(v) { app.setCustomPageHeight(Number(v)); },  // cm; applies when pageSize='custom'
    get theme() { return app.theme; }, set theme(v) { app.setTheme(v); },                        // 'dark' | 'light'
    // Brand accent preset (see stencil.mainThemes for the valid keys). Setting an
    // unknown key throws rather than silently falling back.
    get mainTheme() { return app.accent; },
    set mainTheme(v) {
      const k = str(v).trim().toLowerCase();
      if (!isAccent(k)) throw new Error(`Unknown theme "${v}". Options: ${ACCENTS.map((a) => a.key).join(', ')}`);
      app.setAccent(k);
    },
    get mainThemes() { return ACCENTS.map((a) => a.key); },                                       // available accent keys
    get drawMode() { return app.drawMode; }, set drawMode(v) { app.setDrawMode(String(v).toLowerCase() === 'rect' ? 'rect' : 'line'); },
    get allowFormulas() { return app.allowFormulas; }, set allowFormulas(v) { app.setAllowFormulas(v); },
    get formulaX() { return app.formulaX; }, set formulaX(v) { app.setFormula('x', v); },
    get formulaY() { return app.formulaY; }, set formulaY(v) { app.setFormula('y', v); },
    get fillColor() { return app.defaultFillColor; }, set fillColor(v) { app.setVisualColor('fill', toHexColor(v)); },
    get selectionGlow() { return app.selGlowColor; }, set selectionGlow(v) { app.setVisualColor('selGlow', toHexColor(v)); },
    get hoverRing() { return app.hoverRingColor; }, set hoverRing(v) { app.setVisualColor('hoverRing', toHexColor(v)); },
    get focusRing() { return app.focusRingColor; }, set focusRing(v) { app.setVisualColor('focusRing', toHexColor(v)); },
  });
  const settings = () => guard(settingsAccessors());

  // Decode a video blob URL and capture the frame at `timeSec` to a JPEG data URL.
  const videoFrameDataUrl = (srcUrl, timeSec) => new Promise((resolve, reject) => {
    const v = document.createElement('video');
    v.muted = true; v.preload = 'auto'; v.src = srcUrl;
    let done = false;
    const fail = (msg) => { if (!done) { done = true; URL.revokeObjectURL(srcUrl); reject(new Error(msg)); } };
    v.addEventListener('loadeddata', () => {
      try { v.currentTime = Math.min(Number(timeSec) || 0, Math.max(0, (v.duration || 0) - 0.01)); }
      catch { fail('video seek failed'); }
    });
    v.addEventListener('seeked', () => {
      if (done) return;
      try {
        const k = Math.min(1, 1920 / Math.max(v.videoWidth, v.videoHeight));
        const c = document.createElement('canvas');
        c.width = Math.max(1, Math.round(v.videoWidth * k));
        c.height = Math.max(1, Math.round(v.videoHeight * k));
        c.getContext('2d').drawImage(v, 0, 0, c.width, c.height);
        done = true; URL.revokeObjectURL(srcUrl);
        resolve(c.toDataURL('image/jpeg', 0.92));
      } catch { fail('video frame capture failed (tainted/cross-origin?)'); }
    });
    v.addEventListener('error', () => fail('video failed to load'));
    setTimeout(() => fail('video frame timeout'), 8000);
  });

  // loadImageFromFile decodes async with no promise; poll until the image is in place.
  const waitForImage = (timeoutMs = 8000) => new Promise((resolve) => {
    const start = Date.now();
    const tick = () => {
      if (app.image || Date.now() - start > timeoutMs) resolve();
      else requestAnimationFrame(tick);
    };
    tick();
  });

  stencil = {
    // ── Projects ──
    get current() {
      if (app.activeProjectId != null) return makeProject(app.activeProjectId);
      if (app.storage.incognito) return makeProject(null, true);
      return null;
    },
    // Open in some tab/window — INCLUDING this tab's incognito editor (it's open too).
    get openedProjects() { return incognitoList().concat(savedOpen()); },
    get archivedProjects() {
      const open = openedIds();
      return app.storage.store.list().filter((m) => !open.has(m.id)).map((m) => makeProject(m.id));
    },
    // Only the CURRENT tab's incognito editor is knowable (others report null).
    get incognitoProjects() { return incognitoList(); },
    // Default = currently-open saved projects; flags add the archived/incognito sets.
    getProjects({ archived = false, incognito = false } = {}) {
      let list = savedOpen();
      if (archived) list = list.concat(stencil.archivedProjects);
      if (incognito) list = list.concat(incognitoList());
      return list;
    },
    getProjectByName(name) {
      const n = str(name).trim().toLowerCase();
      const m = app.storage.store.list().find((p) => str(p.name).trim().toLowerCase() === n);
      return m ? makeProject(m.id) : null;
    },

    // ── Settings / modes ──
    get settings() { return settings(); },
    get fullscreen() { return typeof document !== 'undefined' && document.body.classList.contains('fullscreen-mode'); },
    set fullscreen(v) { if (!!v !== stencil.fullscreen && typeof app.toggleFullscreen === 'function') app.toggleFullscreen(); },
    get imageSize() { const img = app.image; return img ? { width: img.width, height: img.height } : undefined; },
    get incognito() { return !!app.storage.incognito; },
    // Incognito can only be turned on for a blank editor (no image yet) — same rule as
    // the toolbar toggle; setting it otherwise throws.
    set incognito(on) {
      if (!!app.storage.incognito === !!on) return;
      if (on && (app.image || app.lines.length || app.activeProjectId != null || !app.storage.temporary))
        throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
      app.storage.incognito = !!on;
      app.updateIncognitoUI();
    },
    // Tooltip sections as a live get/set object.
    get tooltip() {
      return guard({
        get enabled() { return app.tooltipEnabled; }, set enabled(v) { app.setTooltipOption('enabled', v); },
        get page() { return app.tooltipShowPage; }, set page(v) { app.setTooltipOption('page', v); },
        get screen() { return app.tooltipShowScreen; }, set screen(v) { app.setTooltipOption('screen', v); },
        get coords() { return app.tooltipShowCoords; }, set coords(v) { app.setTooltipOption('coords', v); },
      });
    },

    // ── Lines / points ──
    get lines() { return app.lines.map((_, i) => makeLine(i)); },
    // Points of the "current" line — in-progress, else coord-table line, else last line.
    get points() {
      if (app.currentLine) return app.currentLine.points.map((_, i) => makePoint(-1, i));
      const idx = app.coordLineIdx >= 0 ? app.coordLineIdx : app.lines.length - 1;
      return idx >= 0 && app.lines[idx] ? app.lines[idx].points.map((_, i) => makePoint(idx, i)) : [];
    },

    // ── Shortcuts ──
    get shortcuts() {
      const out = {};
      for (const [id, combo] of hotkeys.entries()) out[id] = combo;
      return out;
    },
    // Rebind a shortcut. `oldRef` matches an action id OR its current combo string.
    changeShortcut(oldRef, newCombo) {
      let id = null;
      for (const [aid, combo] of hotkeys.entries()) if (aid === oldRef || combo === oldRef) { id = aid; break; }
      if (!id) throw new Error(`No shortcut matches "${oldRef}"`);
      for (const [aid, combo] of hotkeys.entries()) if (aid !== id && combo === newCombo) throw new Error(`"${newCombo}" is already bound to ${aid}`);
      hotkeys.set(id, newCombo);
      hotkeys.save();
      try { hotkeys.updateCtxHints?.(); hotkeys.updateHotkeyTitles?.(); } catch { /* no DOM */ }
      return stencil;
    },

    // ── Editor actions (chainable) ──
    rotateLeft() { app.rotateImage(-1); return stencil; },
    rotateRight() { app.rotateImage(1); return stencil; },
    undo() { app.undo(); return stencil; },
    redo() { app.redo(); return stencil; },
    startDrawing() { app.startDrawingMode(); return stencil; },
    stopDrawing() { app.stopDrawingMode(); return stencil; },
    // Point-adding mode as a get/set toggle (mirrors the Start/Stop drawing buttons).
    // Enabling needs a loaded image (matches the toolbar's guard).
    get drawing() { return !!app.isDrawing; },
    set drawing(on) {
      if (on) { if (app.image && !app.isDrawing) app.startDrawingMode(); }
      else if (app.isDrawing) app.stopDrawingMode();
    },
    clearLines() { app.clearAllLines(); return stencil; },
    downloadImage() { app.saveImage(); return stencil; },        // download image+lines (PNG)
    copyLayout() { app.copyLayoutToClipboard(); return stencil; },
    copyImage() { app.copyImageToClipboard(); return stencil; },
    downloadLayout() { app.downloadJSON(); return stencil; },
    get layout() { return stencil.current?.layout; },
    set layout(data) { app.applyPastedLayout(data); },

    // Pan the canvas viewport by pixel deltas (positive x → right, y → down).
    move({ x = 0, y = 0 } = {}) {
      const vp = document.getElementById('canvas-viewport');
      if (vp) { vp.scrollLeft += Number(x) || 0; vp.scrollTop += Number(y) || 0; }
      return stencil;
    },

    // Zoom by a relative step (0.25 in, -0.4 out). With `point` ({x,y} in image px) the
    // zoom keeps that point fixed on screen; otherwise it recentres.
    zoom(amount, point) {
      const next = app.zoomPan.clampScale((app.scale || 1) + Number(amount || 0));
      if (point && (point.x != null || point.y != null)) app.zoomPan.zoomToImagePoint(next, Number(point.x) || 0, Number(point.y) || 0);
      else app.zoomPan.zoomAroundCenter(next);
      return stencil;
    },
    // Absolute zoom as a percentage (mirrors the toolbar's zoom % input).
    get zoomLevel() { return Math.round((app.scale || 1) * 100); },
    set zoomLevel(pct) { app.zoomPan.zoomAroundCenter(app.zoomPan.clampScale((Number(pct) || 100) / 100)); },
    // Fit the image to the window (the toolbar's "fit" button).
    zoomFit() { app.zoomPan.fitToWindow(); return stencil; },
    // Start a fresh blank (unsaved) editor — clears image + lines (the toolbar's clear/new).
    newEditor() { app.newEditor(); return stencil; },
    // Create a solid-color blank image to draw on (mirrors the blank-image creator).
    // `color` is any CSS color (default white); opts.size = { width, height } in px
    // (defaults to the current page size). Resolves to the facade once the image loads.
    async blank(color = '#ffffff', opts = {}) {
      const size = opts.size || {};
      await app.createBlankImage({ color, width: size.width, height: size.height });
      await waitForImage();
      return stencil;
    },

    // Crop by axis edges. Each of x1/y1/x2/y2 may be a number (px move of that edge),
    // an absolute length ('3cm'/'-4in'/'50%'/'-60%'; '-' = from the axis end), or be
    // omitted (keep the current edge). Commits via the same applyCrop the UI uses.
    //
    // Proportion fill: when exactly ONE axis is given (any of its edges present) and
    // the other is left out entirely, the missing axis's LENGTH is derived from the
    // page proportion instead of kept as-is, so the crop matches the page's
    // width:height relation. `album` (default false) picks the orientation that
    // proportion is taken in — false (portrait): height = width × (long/short page
    // side), width = height ÷ (…); album true (landscape) is the inverse. The derived
    // axis keeps its current start edge; only its length changes. Giving both axes (or
    // neither) leaves cropping free-form, exactly as before.
    crop(spec = {}) {
      if (!app.originalImage) throw new Error('No image loaded to crop');
      const dims = app.effectiveOriginalDims();   // { w, h } in rotated-original pixels
      const r = app.cropRect || app.defaultCropRect();
      const ps = app.getPageDimensions();
      const pxPerCmX = app.canvas.width / ps.width, pxPerCmY = app.canvas.height / ps.height;
      const edge = (tok, cur, lengthPx, pxPerCm) =>
        tok == null ? cur : resolveAxisPx(tok, { lengthPx, pxPerCm, currentPx: cur });
      let x1 = edge(spec.x1, r.x, dims.w, pxPerCmX);
      let x2 = edge(spec.x2, r.x + r.width, dims.w, pxPerCmX);
      let y1 = edge(spec.y1, r.y, dims.h, pxPerCmY);
      let y2 = edge(spec.y2, r.y + r.height, dims.h, pxPerCmY);

      const xGiven = spec.x1 != null || spec.x2 != null;
      const yGiven = spec.y1 != null || spec.y2 != null;
      if (xGiven !== yGiven) {
        const aspect = cropAspect(ps.width, ps.height, !!spec.album);   // width / height
        if (xGiven) {                                  // have width → derive height
          y1 = r.y; y2 = r.y + Math.abs(x2 - x1) / aspect;
        } else {                                       // have height → derive width
          x1 = r.x; x2 = r.x + Math.abs(y2 - y1) * aspect;
        }
      }
      app.applyCrop({ x: Math.min(x1, x2), y: Math.min(y1, y2), width: Math.abs(x2 - x1), height: Math.abs(y2 - y1) }, { recalc: true });
      return stencil;
    },

    // px → page coords (cm, with active formulas applied).
    px2Page({ x = 0, y = 0 } = {}) { return app.pixelToPageCoords(Number(x), Number(y)); },
    // page (cm) → px. Inverts the linear page mapping; does NOT invert formulas.
    page2Px({ x = 0, y = 0 } = {}) {
      const ps = app.getPageDimensions();
      return { x: (Number(x) / ps.width) * app.canvas.width, y: (Number(y) / ps.height) * app.canvas.height };
    },

    // Bulk-apply from one object, then return the facade for chaining. Any settings key
    // routes through stencil.settings; plus showTooltip/tooltip, fullscreen, incognito,
    // zoom, crop, move, layout. `page` is an alias for pageSize.
    apply(opts = {}) {
      const set = stencil.settings;
      for (const k of [
        'unit', 'color', 'lineColor', 'pointSize', 'markerSize', 'thickness', 'lineStyle',
        'pointStyle', 'showPoints', 'showLines', 'filter', 'filterColor', 'pageSize', 'drawMode',
        'allowFormulas', 'formulaX', 'formulaY', 'fillColor', 'selectionGlow', 'hoverRing', 'focusRing',
      ]) {
        if (opts[k] != null) set[k] = opts[k];
      }
      if (opts.page != null) set.pageSize = opts.page;            // `page` alias for pageSize
      if (opts.showTooltip != null) app.setTooltipOption('enabled', opts.showTooltip);
      if (opts.tooltip && typeof opts.tooltip === 'object')
        for (const k of ['enabled', 'page', 'screen', 'coords'])
          if (opts.tooltip[k] != null) app.setTooltipOption(k, opts.tooltip[k]);
      if (opts.fullscreen != null) stencil.fullscreen = opts.fullscreen;
      if (opts.incognito != null) stencil.incognito = opts.incognito;
      if (opts.zoom != null) stencil.zoom(opts.zoom);
      if (opts.layout != null) stencil.layout = opts.layout;
      if (opts.crop && typeof opts.crop === 'object') stencil.crop(opts.crop);
      if (opts.move && typeof opts.move === 'object') stencil.move(opts.move);
      return stencil;
    },

    // Load an image (or a video frame) by URL into the editor. Resolves to the facade so
    // callers can `(await stencil.load(url)).crop(...)`. `source` defaults to the URL.
    // For a video URL (or when `frame` is given), the frame at `frame` seconds is grabbed.
    async load(url, opts = {}) {
      const resp = await fetch(url);
      if (!resp.ok) throw new Error(`Failed to fetch ${url}: HTTP ${resp.status}`);
      const blob = await resp.blob();
      const type = blob.type || '';
      const baseName = opts.name || decodeURIComponent(url.split('/').pop().split(/[?#]/)[0] || '') || 'image';

      let file;
      if (type.startsWith('video/') || opts.frame != null || opts.usePoster) {
        // Grab a frame from the video (usePoster has no poster on a bare URL → ignored).
        const dataUrl = await videoFrameDataUrl(URL.createObjectURL(blob), Number(opts.frame) || 0);
        const fb = await (await fetch(dataUrl)).blob();
        file = new File([fb], baseName.replace(/\.[^.]+$/, '') + '.jpg', { type: 'image/jpeg' });
      } else if (type.startsWith('image/')) {
        file = new File([blob], baseName, { type });
      } else {
        throw new Error(`Not an image or video (got "${type || 'unknown'}")`);
      }

      const loadOpts = { source: opts.source ?? url, resource: opts.resource ?? '' };
      if (opts.crop) loadOpts.crop = opts.crop;
      app.loadImageFromFile(file, loadOpts);
      await waitForImage();
      return stencil;
    },
  };

  // Flatten the settings accessors onto the facade itself so `stencil.lineColor`,
  // `stencil.showPoints`, `stencil.pageSize`, `stencil.theme`, … work as well as
  // `stencil.settings.<key>` (both drive the same app setters). Copies the get/set
  // descriptors (not values); the keys don't collide with the facade's own members.
  Object.defineProperties(stencil, Object.getOwnPropertyDescriptors(settingsAccessors()));

  // Hide every member from enumeration so `console.log(stencil)` / Object.keys read as
  // a clean object rather than dumping the whole method surface. Access and DevTools
  // autocomplete are unaffected (non-enumerable ≠ inaccessible). Runs before the freeze
  // (freeze locks descriptors).
  for (const k of Reflect.ownKeys(stencil)) {
    const d = Object.getOwnPropertyDescriptor(stencil, k);
    if (d.enumerable) Object.defineProperty(stencil, k, { ...d, enumerable: false });
  }
  // Freeze + hard-guard via the same proxy as every nested object (writing a method or a
  // read-only getter THROWS — `stencil.load = 0` / `stencil.getProjectByName = 0`; the
  // legit setters fullscreen/incognito/layout still write through). Tamper-resistance, not
  // a security boundary — a DevTools user is inside the trust boundary regardless.
  // Reassigning the closure ref means every method that `return stencil`s hands back this
  // guarded proxy, so chaining is unaffected.
  stencil = guard(stencil);
  return stencil;
};
