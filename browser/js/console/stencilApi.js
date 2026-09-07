// ── window.stencil — console control API for the Stencil editor ─────────────
// Thin chainable facade over the live DrawingApp; NEVER reimplements editor behaviour —
// every mutation routes through the same shared core methods the toolbar uses
// (setColor/setPageSize/applyCrop/loadImageFromFile/…), so console and toolbar stay in sync.
// Closure factory (not a class): `app`/state live in scope, returned objects carry no fields
// (no `_app`/`#app` to read/overwrite; DevTools can still reach `app` via [[Scopes]]).
// index.js builds this after the app → window.stencil. Most methods return the facade (or a
// Project/Line/Point) for chaining, e.g.
//   stencil.apply({ page: 'a3', pointSize: 9 }).rotateLeft().crop({ x1: '10%' })
//   (await stencil.load(url)).crop({ x2: '-10%' }).apply({ lineColor: 'aqua' })
import { hotkeys } from '../core/hotkeys.js';
import { resolveAxisPx } from '../core/units.js';
import { cropAspect, scaleCropCentered } from '../core/cropGeometry.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { PERIOD_ORDER, DEFAULT_PERIOD } from '../core/projectsStore.js';
import { parseDuration } from '../core/durationParser.js';
import { ACCENTS, isAccent, normalizeHex, toHexColor } from '../core/accents.js';
import { pointColorOf } from '../core/renderer.js';
import { ConnectionManager } from '../net/connectionManager.js';
import { loadSavedServers, saveServers, getAutoConnect } from '../net/connectionStore.js';
import { requireConnection } from '../net/remoteSync.js';
import { notify } from '../utils.js';
import { motionPrefs, MOTION_MODES } from '../ui/motionPrefs.js';
import { videoFrameDataUrl } from '../core/videoFrame.js';
import { loadLlmSettings, saveLlmSettings, PROVIDERS, withProvider } from '../llm/llmSettings.js';
import { loadVoiceSettings, saveVoiceSettings, isLanguageTag, clampSilenceMs, SILENCE_MS_MIN, SILENCE_MS_MAX } from '../llm/voiceSettings.js';
import {
  chatSide, setChatSide, applyChatSide, CHAT_SIDE_SWAPPED,
} from '../ui/chatLayoutPrefs.js';

// A layout argument may be an OBJECT or a raw JSON string — parse the latter so callers
// can hand over clipboard/file text directly. A non-object (or bad JSON) throws, since
// silently applying nothing would look like the layout was accepted.
const toLayoutObject = (data) => {
  if (typeof data !== 'string') return data;
  let parsed;
  try { parsed = JSON.parse(data); } catch { throw new TypeError('stencil: layout JSON could not be parsed'); }
  if (parsed == null || typeof parsed !== 'object') throw new TypeError('stencil: layout JSON must describe an object');
  return parsed;
};

const str = (v) => (v == null ? '' : String(v));

// Help text for stencil.expire() — shown when it's called with no argument. The
// grammar is DurationParser's (durationParser.js / core/parse/durationParser.cpp).
const DURATION_HELP = [
  'stencil.expire(spec) — set when the active project expires, from a duration:',
  "  a unit alone (one of it):  'day' · 'week' · 'fortnight' · 'month' · 'year'",
  "  a count + unit (either order):  'days 23' · 'months 3' · '3 weeks'",
  "  keep forever:  'off' · 'never' · 'none'",
  'Called with no argument this prints these formats; with one it applies the expiry.',
].join('\n');

export const createStencil = (app) => {
  // One ConnectionManager per session, shared with the connection UI via app.connections;
  // onChange fires a DOM event so the projects modal / connect dialog can refresh.
  const firstInit = !app.connections;
  const connMgr = app.connections || (app.connections = new ConnectionManager({
    onChange: (change) => {
      // Persist the live set so it survives reloads (connectionStore.js), then let
      // the connect/projects UI refresh off the same DOM event.
      try { saveServers(connMgr.snapshot()); } catch { /* storage blocked */ }
      // Live co-edit: forward a server project-event to the editor so it can reload the
      // active project when a peer changes it.
      if (change && change.type === 'event' && change.message?.type === 'project-event') {
        try { app.remoteSync?.onServerProjectEvent?.(change.message, change.connection); } catch { /* editor not ready */ }
      }
      try {
        window.dispatchEvent(new Event('stencil:connections-changed'));
      } catch { /* no DOM */ }
    },
  }));
  // On first boot, optionally re-establish the saved server set (the "auto-connect on
  // open" preference, default on). Connect each independently so one dead server doesn't
  // block the rest, and report each unreachable address in its own corner toast.
  if (firstInit && getAutoConnect()) {
    const saved = loadSavedServers();
    // A credential the server ALREADY refused is never retried — it cannot start working.
    // The row is adopted straight into the expired set instead, so the Servers button
    // still carries its dot and the row still offers Reconnect.
    const dead = saved.filter((s) => s.expired);
    const live = saved.filter((s) => !s.expired);
    for (const s of dead) { try { connMgr.adoptExpired(s); } catch { /* bad url — skip */ } }
    if (dead.length) {
      console.warn(`stencil: ${dead.length} saved server session(s) need signing in again — Servers ▸ Reconnect`);
    }
    if (live.length) {
      Promise.allSettled(live.map((s) => connMgr.connect(s))).then((results) => {
        // Results stay positional with `live`, so a failure can be traced back to the
        // server it belongs to.
        const rejected = results
          .map((r, i) => ({ r, url: live[i].url }))
          .filter(({ r }) => r.status === 'rejected');
        // A REFUSED credential is not an unreachable server: the server answered, this
        // session is simply over — say it needs a new token and open the Connections
        // modal on click, rather than sending the user hunting a server that is up.
        const expired = rejected.filter(({ r }) => r.reason?.expired);
        const unreachable = rejected.filter(({ r }) => !r.reason?.expired);
        if (expired.length) {
          console.warn(`stencil: ${expired.length} saved server session(s) expired — reconnect from Connections`);
          notify(`Session expired on ${expired.length} saved server${expired.length === 1 ? '' : 's'} — reconnect`, 'fail',
            { onClick: () => document.getElementById('connect-btn')?.click() });
        }
        // One toast per address, not a count — a count says nothing about WHICH
        // server to go check.
        for (const { url } of unreachable) notify(`Couldn't reach ${url}`, 'info');
      });
    }
  }
  let peers = [];
  try { app.tabs.onPeers((ids) => { peers = Array.isArray(ids) ? ids : []; }); } catch { /* no coordinator */ }
  const openedIds = () => {
    const ids = new Set(peers.filter((x) => x != null));
    if (app.activeProjectId != null) ids.add(app.activeProjectId);   // own active id, defensively
    return ids;
  };

  let stencil;   // forward ref so wrappers can return the facade for chaining

  // Hard-guard an API object: real setters write through, but writing a method/read-only
  // getter (or add/delete) THROWS instead of silently no-opping in the non-strict console.
  // Applied to the facade + every Line/Point/Project/settings object handed back (so e.g.
  // `stencil.lines[0].move = 3`, `stencil.load = 0`, `pt.remove = 1` is rejected).
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // Dismiss any open editor modal (projects, shortcuts/info, links, visuals, …) so a
  // console-driven load isn't hidden behind one. Toggles the shared .modal-open class;
  // each modal's onClose cleanup is idempotent and re-runs on next open.
  const closeModals = () => {
    try { document.querySelectorAll('.app-modal-overlay.modal-open').forEach((o) => o.classList.remove('modal-open')); }
    catch { /* no DOM (node tests) */ }
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
      // Absolute set of x/y (and optionally the parent line's point size).
      apply({ x, y, size } = {}) {
        if (x != null) app.setPointCoord(lineIdx, ptIdx, 'x', x);
        if (y != null) app.setPointCoord(lineIdx, ptIdx, 'y', y);
        if (size != null) {
          const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
          if (line) { line.pointSize = Number(size); app.saveHistory(); app.renderer.redraw(); }
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
      // Recolouring the stroke pins an inherit-fallback point colour first, so already-drawn
      // points keep their rendered colour (same rule as applySelectionChange).
      get color() { return obj()?.color; },
      set color(v) {
        const l = obj();
        if (!l) return;
        if (!l.pointColor) l.pointColor = l.color;
        l.color = toHexColor(str(v));
        commit();
      },
      // Point colour for THIS line. Reading reports the colour it actually draws in
      // (falling back to the stroke); assigning null/'' clears it back to that fallback.
      get pointColor() { const l = obj(); return l ? pointColorOf(l) : undefined; },
      set pointColor(v) { setProp('pointColor', v == null || v === '' ? '' : toHexColor(str(v))); },
      get thickness() { return obj()?.thickness; }, set thickness(v) { setProp('thickness', Number(v)); },
      get pointSize() { return obj()?.pointSize; }, set pointSize(v) { setProp('pointSize', Number(v)); },
      get style() { return obj()?.style; }, set style(v) { setProp('style', str(v)); },
      get fillColor() { return obj()?.fillColor; }, set fillColor(v) { setProp('fillColor', v == null ? 'transparent' : toHexColor(str(v))); },
      // Batch style update. Accepts color/pointColor/thickness/pointSize/style/fillColor.
      apply(opts = {}) {
        const l = obj();
        if (!l) return line;
        if (opts.color != null) {
          if (!l.pointColor) l.pointColor = l.color;   // see the color setter
          l.color = toHexColor(str(opts.color));
        }
        if (opts.pointColor != null) l.pointColor = opts.pointColor === '' ? '' : toHexColor(str(opts.pointColor));
        if (opts.thickness != null) l.thickness = Number(opts.thickness);
        const ms = opts.pointSize;
        if (ms != null) l.pointSize = Number(ms);
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
    // Update a provenance link live: active project via app state + save; a stored
    // (maybe open-in-another-tab) project via the registry + a broadcast.
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
    // Normalize `v` (a Date, epoch ms, a parseable date string, or 0/null = keep forever)
    // and apply it through the shared core setter, which propagates to the server for a
    // server-linked project.
    const setExpiry = (v, what) => {
      if (incognito) throw new Error('Cannot set expiration on an incognito editor');
      if (v == null || v === 0) { app.setProjectExpiration(id, { expiresAt: 0 }); return; }
      const ms = v instanceof Date ? v.getTime() : (typeof v === 'number' ? v : new Date(v).getTime());
      if (!Number.isFinite(ms)) throw new Error(`Invalid ${what} "${v}" — use a Date, epoch ms, a date string, or 0/null to keep forever`);
      if (ms < Date.now()) throw new Error('Expiration cannot be in the past');
      if (app.setProjectExpiration(id, { expiresAt: ms }) == null) throw new Error(`Could not set expiration on project ${id}`);
    };
    let project = {
      get id() { return id; },
      get incognito() { return incognito; },
      get isOpened() { return incognito ? true : openedIds().has(id); },
      // Expiration. `expiresAt` is epoch ms (or null = kept forever); `expirationDate` is
      // the same value as a Date. Both setters accept a number (ms), a Date, or a date
      // string; 0/null keeps forever. Writes take the expiration modal's core path.
      get expiresAt() { const m = meta(); return m ? store().expiresAt(m) : null; },
      set expiresAt(v) { setExpiry(v, 'expiration'); },
      get expirationDate() { const m = meta(); const ms = m ? store().expiresAt(m) : null; return ms ? new Date(ms) : null; },
      set expirationDate(v) { setExpiry(v, 'expiration date'); },
      get isExpired() { const m = meta(); return m ? store().isExpired(m) : false; },
      // Refresh preset used by renew() and the open-time auto-refresh.
      get refreshPeriod() { return meta()?.refreshPeriod ?? DEFAULT_PERIOD; },
      set refreshPeriod(v) {
        if (incognito) throw new Error('Cannot set a refresh period on an incognito editor');
        const p = str(v);
        if (!PERIOD_ORDER.includes(p)) throw new Error(`Invalid refresh period "${v}" — one of ${PERIOD_ORDER.join(', ')}`);
        if (app.setProjectExpiration(id, { refreshPeriod: p }) == null) throw new Error(`Could not set refresh period on project ${id}`);
      },
      // When true, opening the project restamps its expiration to now + refreshPeriod.
      get autoRefresh() { return meta()?.autoRefresh !== false; },
      set autoRefresh(v) {
        if (incognito) throw new Error('Cannot set auto-refresh on an incognito editor');
        if (app.setProjectExpiration(id, { autoRefresh: !!v }) == null) throw new Error(`Could not set auto-refresh on project ${id}`);
      },
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
      // Custom accent colour painting this project's name: "#rrggbb" or '' (theme accent).
      get color() { return incognito ? '' : (meta()?.color ?? ''); },
      set color(v) {
        if (incognito) throw new Error('Cannot color an incognito editor');
        const s = str(v).trim();
        if (s && !normalizeHex(s)) throw new Error(`Invalid project color "${v}" — use a hex like #ff5623, or '' to clear`);
        if (app.setProjectColor(id, s) == null) throw new Error(`Could not set color on project ${id}`);
      },
      // Search keywords (string[]). Assign an array or a comma/space-separated string to
      // replace them; addKeywords / removeKeywords adjust the set. Mirrors the CLI /keywords.
      get keywords() { return incognito ? [] : (meta()?.keywords ?? []).slice(); },
      set keywords(v) {
        if (incognito) throw new Error('Cannot set keywords on an incognito editor');
        const list = Array.isArray(v) ? v : str(v).split(/[\s,]+/);
        if (app.setProjectKeywords(id, list) == null) throw new Error(`Could not set keywords on project ${id}`);
      },
      // Whether this is a blank-image project (solid-colour background). Read-only.
      get blank() { return incognito ? false : !!meta()?.blank; },
      // True when this project was opened from a portable .stencil file (drives the bronze
      // projects-list outline / badge). Read-only provenance flag.
      get fromFile() { return incognito ? false : !!meta()?.fromFile; },
      // Blank-fill colour ("#rrggbb"), or null for a non-blank project. Assigning recolours the
      // solid background in place (the drawn lines stay). Setting on a non-blank project is a no-op
      // (throws), per the "only blanks have a blank colour" rule.
      get blankColor() { const m = meta(); return (m && m.blank) ? (m.blankColor || '') : null; },
      set blankColor(v) {
        if (incognito) throw new Error('Cannot recolor an incognito editor');
        if (!meta()?.blank) throw new Error(`Project ${id} is not a blank image — nothing to recolor`);
        const s = str(v).trim();
        if (!normalizeHex(s)) throw new Error(`Invalid blank color "${v}" — use a hex like #ffffff`);
        if (app.setProjectBlankColor(id, s) == null) throw new Error(`Could not set blank color on project ${id}`);
      },
      addKeywords(...kw) {
        if (incognito) throw new Error('Cannot set keywords on an incognito editor');
        const add = kw.flatMap((k) => (Array.isArray(k) ? k : str(k).split(/[\s,]+/))).filter(Boolean);
        if (app.setProjectKeywords(id, [...(meta()?.keywords ?? []), ...add]) == null) throw new Error(`Could not add keywords on project ${id}`);
        return project;
      },
      removeKeywords(...kw) {
        if (incognito) throw new Error('Cannot set keywords on an incognito editor');
        const drop = new Set(kw.flatMap((k) => (Array.isArray(k) ? k : str(k).split(/[\s,]+/))).map((s) => str(s).trim().toLowerCase()).filter(Boolean));
        const cur = (meta()?.keywords ?? []).filter((k) => !drop.has(str(k).toLowerCase()));
        if (app.setProjectKeywords(id, cur) == null) throw new Error(`Could not remove keywords on project ${id}`);
        return project;
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
      // Set expiry from a free-form duration ("days 23", "fortnight", "off"). No/blank arg
      // returns the format help. Routes through setProjectExpiration so a server-linked
      // project propagates the new expiry to the server.
      expire(spec) {
        if (incognito) throw new Error('Cannot set expiration on an incognito editor');
        const s = str(spec).trim();
        if (!s) return DURATION_HELP;
        const ms = parseDuration(s);
        if (ms == null) throw new Error(`Invalid duration "${spec}". ${DURATION_HELP}`);
        const expiresAt = ms === 0 ? 0 : Date.now() + ms;
        if (app.setProjectExpiration(id, { expiresAt }) == null) throw new Error(`Could not set expiration on project ${id}`);
        return project;
      },
      // Remove the expiration date so the project is kept forever.
      keepForever() {
        if (incognito) throw new Error('Cannot change expiration on an incognito editor');
        app.setProjectExpiration(id, { expiresAt: 0 });
        return project;
      },
      // Close this project's editor (keeps the saved project). `fully` also closes the
      // browser tab/window if it's the active one here.
      close({ fully = false } = {}) {
        if (incognito || isActive()) app.closeProject(app.activeProjectId, { fully });
        else app.closeProject(id, { fully });
        return project;
      },
      open() { if (id != null) app.switchToProject(id); return project; },
      // Permanently remove this project (an incognito editor can't be removed — close() it).
      remove() {
        if (incognito) throw new Error('Cannot remove an incognito editor — use close()');
        app.removeProject(id);
        return null;
      },
      // Move this LOCAL project to a server (it becomes server-backed). Returns the remote id.
      moveToServer(address) {
        if (incognito) throw new Error('Cannot move an incognito editor — use stencil.publishIncognito(address)');
        return app.moveProjectToServer(id, address);
      },
      // Copy this LOCAL project to a server, leaving the local one in place. opts: { name }.
      copyToServer(address, opts = {}) {
        if (incognito) throw new Error('Cannot copy an incognito editor — use stencil.publishIncognito(address)');
        return app.copyProjectToServer(id, address, opts);
      },
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
    get lineColor() { return app.color; }, set lineColor(v) { app.settings.setColor(toHexColor(v)); },
    // Default point colour for NEW lines; '' (or null) means "follow lineColor".
    get pointColor() { return app.pointColor; },
    set pointColor(v) { app.settings.setPointColor(v == null || v === '' ? '' : toHexColor(v)); },
    get thickness() { return app.thickness; }, set thickness(v) { app.settings.setThickness(v); },
    get pointSize() { return app.pointSize; }, set pointSize(v) { app.settings.setPointSize(v); },
    get lineStyle() { return app.style; }, set lineStyle(v) { app.settings.setLineStyle(v); },
    get pointStyle() { return app.showPoints; }, set pointStyle(v) { app.settings.setShowPoints(v); },   // points visible?
    get showPoints() { return app.showPoints; }, set showPoints(v) { app.settings.setShowPoints(v); },
    get showLines() { return app.showLines; }, set showLines(v) { app.settings.setShowLines(v); },
    get filter() { return app.imageFilter; }, set filter(v) { app.settings.setImageFilter(v); },   // 'none'|'bw'|'sepia'|'invert'|'contour'|'custom'
    get compareMode() { return app.compareMode; }, set compareMode(v) { app.settings.setCompareMode(v); },   // 'none'|'original'|'vertical'|'horizontal' — hold original vs current edit
    get compareSplit() { return app.compareSplit; }, set compareSplit(v) { app.settings.setCompareSplit(v); },   // divider position 0..1 for the split modes
    get filterColor() { return app.filterColor; }, set filterColor(v) { app.settings.setFilterColor(toHexColor(v)); },
    get unit() { return app.unit; }, set unit(v) { app.settings.setUnit(v); },
    get pageSize() { return app.pageSize; }, set pageSize(v) { app.settings.setPageSize(v); },            // case-insensitive: any ISO name A0–C10 ('a3', 'b5', …) or 'custom'
    get pageWidth() { return app.customPageWidth; }, set pageWidth(v) { app.settings.setCustomPageWidth(Number(v)); },     // cm; applies when pageSize='custom'
    get pageHeight() { return app.customPageHeight; }, set pageHeight(v) { app.settings.setCustomPageHeight(Number(v)); },  // cm; applies when pageSize='custom'
    get darkTheme() { return app.theme === 'dark'; }, set darkTheme(v) { app.setTheme(v ? 'dark' : 'light'); },   // dark mode on/off
    // Brand accent: a preset key (see stencil.mainThemes) persists + syncs across tabs; a
    // hex like '#ff5623' applies to THIS page only (not saved, not synced). Anything else
    // throws. The getter returns the active custom hex if set, otherwise the preset key.
    get mainTheme() { return app.customAccent || app.accent; },
    set mainTheme(v) {
      const s = str(v).trim();
      const k = s.toLowerCase();
      if (isAccent(k)) { app.setAccent(k); return; }
      const hex = normalizeHex(s);
      if (hex) { app.setCustomAccent(hex); return; }
      throw new Error(`Unknown theme "${v}". Use a hex like #ff5623, or one of: ${ACCENTS.map((a) => a.key).join(', ')}`);
    },
    get mainThemes() { return ACCENTS.map((a) => a.key); },                                       // available accent keys
    // Active project's accent colour — the custom colour its NAME is painted in. Getter
    // returns the stored "#rrggbb" or '' (no custom colour → theme accent). Setter routes to
    // DrawingApp.setProjectColor: '' clears it, a valid hex sets it, anything else throws.
    get projectColor() {
      const id = app.activeProjectId;
      return id != null ? (app.storage.store.getMeta(id)?.color || '') : '';
    },
    set projectColor(v) {
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to color');
      const s = str(v).trim();
      if (s && !normalizeHex(s)) throw new Error(`Invalid project color "${v}" — use a hex like #ff5623, or '' to clear`);
      app.setProjectColor(id, s);
    },
    // Active project's free-text description ('' when unset) and search keywords (string[]).
    // Both need an active project to write to (same rule as projectColor); the keywords
    // setter also takes a comma/space-separated string, and the store normalizes the list.
    get description() {
      const id = app.activeProjectId;
      return id != null ? (app.storage.store.getMeta(id)?.description || '') : '';
    },
    set description(v) {
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to describe');
      if (app.setProjectDescription(id, str(v)) == null) throw new Error(`Could not set description on project ${id}`);
    },
    get keywords() {
      const id = app.activeProjectId;
      return id != null ? (app.storage.store.getMeta(id)?.keywords ?? []).slice() : [];
    },
    set keywords(v) {
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to tag');
      const list = Array.isArray(v) ? v : str(v).split(/[\s,]+/);
      if (app.setProjectKeywords(id, list) == null) throw new Error(`Could not set keywords on project ${id}`);
    },
    get drawMode() { return app.drawMode; }, set drawMode(v) { app.setDrawMode(String(v).toLowerCase() === 'rect' ? 'rect' : 'line'); },
    // Hold-to-draw hold/dwell delay in milliseconds (clamped 100–3000). See holdDraw.js.
    get holdDrawDelay() { return app.holdDrawDelay; }, set holdDrawDelay(v) { app.input.setHoldDrawDelay(v); },
    get allowFormulas() { return app.allowFormulas; }, set allowFormulas(v) { app.settings.setAllowFormulas(v); },
    get formulaX() { return app.formulaX; }, set formulaX(v) { app.settings.setFormula('x', v); },
    get formulaY() { return app.formulaY; }, set formulaY(v) { app.settings.setFormula('y', v); },
    // ── Motion (ui/motionPrefs.js; the Visuals modal's Motion section) ──
    // The canvas stroke animation — a new vertex flying to where it was put, its
    // landing pop and ripple. Off puts every point straight down.
    get drawingAnimations() { return motionPrefs().drawing; },
    set drawingAnimations(v) { app.settings.setMotion('drawing', !!v); },
    // How the INTERFACE moves: 'particles' (windows, menus, marks and the canvas form
    // out of dust), 'slide' (no dust — each surface plays its own plain entrance) or
    // 'none' (nothing moves). prefers-reduced-motion still wins on its own.
    get motionMode() { return motionPrefs().mode; },
    set motionMode(v) { app.settings.setMotion('mode', v); },
    get motionModes() { return MOTION_MODES.slice(); },
    get fillColor() { return app.defaultFillColor; }, set fillColor(v) { app.settings.setVisualColor('fill', toHexColor(v)); },
    get selectionGlow() { return app.selGlowColor; }, set selectionGlow(v) { app.settings.setVisualColor('selGlow', toHexColor(v)); },
    get hoverRing() { return app.hoverRingColor; }, set hoverRing(v) { app.settings.setVisualColor('hoverRing', toHexColor(v)); },
    get focusRing() { return app.focusRingColor; }, set focusRing(v) { app.settings.setVisualColor('focusRing', toHexColor(v)); },
    // Voice input (js/llm/voiceSettings.js — its own store, shared by dictation and voice
    // chat). Language: 'default' (English) or any BCP-47 tag like 'de-DE'; a live
    // recognizer switches at once. Silence: the pause that sends, clamped 500–10000 ms.
    get voiceInputLanguage() { return loadVoiceSettings().language; },
    set voiceInputLanguage(v) {
      const lang = str(v).trim();
      const isDefault = lang === '' || lang.toLowerCase() === 'default';
      if (!isDefault && !isLanguageTag(lang)) throw new Error(`Invalid voice input language "${v}" — use 'default' or a BCP-47 tag like en-US`);
      saveVoiceSettings({ ...loadVoiceSettings(), language: isDefault ? 'default' : lang });
    },
    get voiceSilenceMs() { return loadVoiceSettings().silenceMs; },
    set voiceSilenceMs(v) {
      if (!Number.isFinite(Number(v))) throw new Error(`Invalid voice silence "${v}" — milliseconds between ${SILENCE_MS_MIN} and ${SILENCE_MS_MAX}`);
      saveVoiceSettings({ ...loadVoiceSettings(), silenceMs: clampSilenceMs(v) });
    },
  });
  const settings = () => guard(settingsAccessors());

  // Validate + persist a partial LLM-settings update through the SAME store the
  // assistant's gear dialog uses (llmSettings.js), so UI and scripting stay in sync.
  const applyLlmSetup = (opts = {}) => {
    const cur = loadLlmSettings();
    let next = { ...cur };
    if (opts.provider != null) {
      const p = str(opts.provider).trim();
      if (!PROVIDERS.includes(p)) throw new Error(`Unknown LLM provider "${opts.provider}" — one of ${PROVIDERS.join(', ')}`);
      // Switching providers refills the default base URL unless the user overrode it
      // (withProvider — literally the settings modal's rule); an explicit baseUrl
      // below still wins.
      if (p !== cur.provider) next = withProvider(next, p);
    }
    for (const k of ['baseUrl', 'serverUrl']) {
      if (opts[k] != null) {
        const v = str(opts[k]).trim();
        if (v && !/^https?:\/\//i.test(v)) throw new Error(`${k} must be an http(s) URL`);
        next[k] = v;
      }
    }
    if (opts.model != null) next.model = str(opts.model).trim();
    if (opts.apiKey != null) next.apiKey = str(opts.apiKey);
    saveLlmSettings(next);
    // Same live-refresh signal the settings modal fires (panel re-probes status).
    try { window.dispatchEvent(new Event('stencil:llm-settings-changed')); } catch { /* no DOM */ }
    return next;
  };

  // The chat panel registers its scripting surface as app.chat when it wires.
  const chatPanel = () => {
    if (!app.chat) throw new Error('Chat panel not ready — the editor UI has not wired yet');
    return app.chat;
  };

  // "Swap message sides" (chatLayoutPrefs.js) is deliberately NOT persisted across a
  // reload/reopened tab — this setter is the one way to adjust it, live, for the rest
  // of this tab's session. Both transcripts share the one preference, so restamp
  // whichever is currently mounted (the panel, the ctx-assist flyout, or neither).
  const applyChatSideEverywhere = (side) => {
    if (typeof document === 'undefined') return;
    applyChatSide(document.getElementById('chat-transcript'), side);
    applyChatSide(document.getElementById('ctx-assist-transcript'), side);
  };

  // loadImageFromFile decodes async with no promise; poll until the image is in place.
  // `previous` = the image loaded BEFORE the call: replacing must wait for the swap, not
  // for "some image exists", or ops chained after a load (a §2.1 `image` op's
  // filter/layout) run against the OLD picture and are wiped by the late decode.
  const waitForImage = (timeoutMs = 8000, previous = null) => new Promise((resolve) => {
    const start = Date.now();
    const again = typeof requestAnimationFrame === 'function'
      ? requestAnimationFrame : (fn) => setTimeout(fn, 16);   // node --test has no rAF
    const tick = () => {
      if ((app.image && app.image !== previous) || Date.now() - start > timeoutMs) resolve();
      else again(tick);
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
    // Local projects whose keywords match ANY of the query terms (case-insensitive substring),
    // mirroring the CLI /keywords-search. Returns Project handles, most-recently-updated first.
    getProjectsByKeyword(...keywords) {
      const terms = keywords.flatMap((k) => (Array.isArray(k) ? k : str(k).split(/[\s,]+/)))
        .map((s) => str(s).trim().toLowerCase()).filter(Boolean);
      if (!terms.length) return [];
      return app.storage.store.list()
        .filter((p) => (p.keywords || []).some((kw) => terms.some((t) => str(kw).toLowerCase().includes(t))))
        .map((p) => makeProject(p.id));
    },
    // Set when the ACTIVE project expires, from a free-form duration; no argument returns
    // the accepted formats. Delegates to the same Project.expire() used for chaining,
    // e.g. stencil.current.expire('months 3').
    expire(spec) {
      const s = str(spec).trim();
      if (!s) return DURATION_HELP;
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to set an expiration on — open or create one first');
      return makeProject(id).expire(s);
    },

    // ── Server connections ──
    // Connect one or more collaboration servers for the current session and gain
    // access to their stored/shared projects. Accepts a URL string, { url, token },
    // or an array of either. Resolves to the facade for chaining.
    //   await stencil.connect('http://host:8090')
    //   await stencil.connect(['a:8090', { url: 'b:8090', token: 't' }])
    async connect(urlOrUrls) { await connMgr.connect(urlOrUrls); return stencil; },
    // Close one connection by URL, or (no arg) the most recently opened one.
    disconnect(url) { connMgr.disconnect(url); return stencil; },
    // Re-establish the last connected set (re-validates/re-issues tokens).
    async reconnect() { await connMgr.reconnect(); return stencil; },
    // Read-only list of connected server URLs.
    get connections() { return connMgr.urls; },
    // Aggregated remote projects across every connection (each tagged remote:true
    // with its serverUrl). Resolves to an array of metadata records.
    serverProjects() { return connMgr.remoteProjects(); },
    // Move/copy a SERVER project (a record from serverProjects(), shape { serverUrl, id, … })
    // to local storage, or copy it into an incognito session (opts.newTab opens a new tab).
    moveServerProjectToLocal(meta) { return app.moveProjectToLocal(meta); },
    copyServerProjectToLocal(meta, opts = {}) { return app.copyServerProjectToLocal(meta, opts); },
    copyServerProjectToIncognito(meta, opts = {}) { return app.copyServerProjectToIncognito(meta, opts); },
    // Publish the current incognito session to a server (becomes a normal server project).
    publishIncognito(address) { return app.publishIncognitoToServer(address); },

    // ── AI assistant (LLM) ──
    // Settings mirror the chat panel's gear dialog (llm-contract.md §5) and
    // persist to the same drawingApp_llmSettings store, so UI and scripting stay in
    // sync. apiKey reads back as-is — the same trust stance as server tokens
    // (connections snapshot); a DevTools user is inside the trust boundary.
    //   stencil.llm.setup({ provider: 'ollama', model: 'llama3.2-vision' })
    //   stencil.llm.provider = 'stencil-server'
    get llm() {
      return guard({
        get provider() { return loadLlmSettings().provider; },
        set provider(v) { applyLlmSetup({ provider: v }); },
        get baseUrl() { return loadLlmSettings().baseUrl; },
        set baseUrl(v) { applyLlmSetup({ baseUrl: v }); },
        get model() { return loadLlmSettings().model; },
        set model(v) { applyLlmSetup({ model: v }); },
        get apiKey() { return loadLlmSettings().apiKey; },
        set apiKey(v) { applyLlmSetup({ apiKey: v }); },
        get serverUrl() { return loadLlmSettings().serverUrl; },
        set serverUrl(v) { applyLlmSetup({ serverUrl: v }); },
        // Partial update in one call; unknown providers / non-http(s) URLs throw.
        setup(opts = {}) { applyLlmSetup(opts); return stencil; },
      });
    },
    // Run one assistant turn through the SAME pipeline the chat panel uses — shared
    // history, the exchange rendered in the panel's transcript. `images` attaches
    // base64 data: URLs to this turn. Resolves { reply, warnings, results } with
    // results = [{ label, dataUrl }]; typed LlmErrors (truncated/refusal/…) reject.
    //   await stencil.prompt('rotate left and give me a sepia variant')
    prompt(text, { images = [] } = {}) {
      return chatPanel().prompt(str(text), Array.isArray(images) ? images : [images]);
    },
    // Chat panel control — the same code paths as the panel's own buttons.
    get chat() {
      return guard({
        open() { chatPanel().open(); return stencil; },
        close() { chatPanel().close(); return stencil; },
        dock(mode) { chatPanel().dock(mode); return stencil; },
        get isOpen() { return !!app.chat && app.chat.isOpen(); },
        // The settled transcript (contract §12.1 display form): [{ role, text }]
        // copies — no raw model JSON, no error cards, no in-flight row.
        get history() { return chatPanel().history(); },
        // Stop the in-flight turn (the Stop button's path). True when a turn
        // was actually running.
        abort() { return chatPanel().abort(); },
        // Fresh conversation — the trash button's exact path (history, queued
        // attachments, transcript, and the §12 persisted copy). Throws mid-turn.
        clear() { chatPanel().clear(); return stencil; },
        get isSending() { return !!app.chat && app.chat.isSending; },
        // "Swap message sides" — which side user/assistant/error bubbles draw on.
        // Scoped to THIS tab's session only: never persisted, so it resets on the
        // next reload/reopen (chatLayoutPrefs.js) — this is the one way to adjust
        // it outside the panel's own menu item.
        get swapSides() { return chatSide() === CHAT_SIDE_SWAPPED; },
        set swapSides(v) {
          setChatSide(v ? CHAT_SIDE_SWAPPED : 'normal');
          applyChatSideEverywhere();
        },
        // Dictation into the panel's composer — the mic face (the "…" item, a
        // double-click or a hold on Send). Turning it on stops the hands-free voice chat.
        get voiceInput() { return !!app.chat && app.chat.voiceInput; },
        set voiceInput(v) { chatPanel().setVoiceInput(!!v); },
      });
    },

    // ── Browser extension ──
    // The extension's editor-page API (open editor tabs, other tabs' images, import into THIS
    // tab), installed on window.__stencilExt by its content script — the extension owns the
    // shape (extension/README.md). null unless installed AND its editor-page setting is on.
    get extension() { return (typeof window !== 'undefined' && window.__stencilExt) || null; },

    // ── Settings / modes ──
    get settings() { return settings(); },
    get fullscreen() { return typeof document !== 'undefined' && document.body.classList.contains('fullscreen-mode'); },
    set fullscreen(v) { if (!!v !== stencil.fullscreen && typeof app.toggleFullscreen === 'function') app.toggleFullscreen(); },
    get imageSize() { const img = app.image; return img ? { width: img.width, height: img.height } : undefined; },
    // Current crop rect in rotated-original px — canonical {x,y,w,h} plus legacy
    // width/height aliases. null before an image loads. The LLM plan executor reads it
    // around crop() to re-map later plan coordinates (llm-contract.md §1); read-only copy.
    get cropRect() { const r = app.cropRect; return r ? { x: r.x, y: r.y, w: r.width, h: r.height, width: r.width, height: r.height } : null; },
    get incognito() { return !!app.storage.incognito; },
    // Incognito can only be turned on for a blank editor (no image yet) — same rule as
    // the toolbar toggle; setting it otherwise throws.
    set incognito(on) {
      if (!!app.storage.incognito === !!on) return;
      if (on && (app.image || app.lines.length || app.activeProjectId != null || !app.storage.temporary))
        throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
      // Turning it OFF with work on screen keeps the work: leaving incognito is the user
      // asking to save, so it promotes to a local project (see promoteIncognitoToLocal)
      // instead of stranding the picture in a session that can never be saved.
      if (!on && app.image) { app.promoteIncognitoToLocal(); return; }
      app.storage.incognito = !!on;
      app.updateIncognitoUI();
    },
    // Leave incognito and keep the current picture + lines as a local project. Returns the
    // project id (null when the editor is blank). The server twin is publishIncognito().
    promoteIncognito() { return app.promoteIncognitoToLocal(); },
    // Tooltip sections as a live get/set object.
    get tooltip() {
      return guard({
        get enabled() { return app.tooltipEnabled; }, set enabled(v) { app.settings.setTooltipOption('enabled', v); },
        get page() { return app.tooltipShowPage; }, set page(v) { app.settings.setTooltipOption('page', v); },
        get screen() { return app.tooltipShowScreen; }, set screen(v) { app.settings.setTooltipOption('screen', v); },
        get coords() { return app.tooltipShowCoords; }, set coords(v) { app.settings.setTooltipOption('coords', v); },
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
    rotateLeft() { app.imageModel.rotateImage(-1); return stencil; },
    rotateRight() { app.imageModel.rotateImage(1); return stencil; },
    // Transform the SELECTED line about its bbox centre (same pivot as the per-line rotate) —
    // flip left↔right / top↔bottom, or rotate a quarter turn ±90. No selection is a no-op.
    flipH() { app.flipSelectedLine(true); return stencil; },
    flipV() { app.flipSelectedLine(false); return stencil; },
    rotate90() { app.rotateSelectedLineQuarter(1); return stencil; },
    rotateMinus90() { app.rotateSelectedLineQuarter(-1); return stencil; },
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
    // Hands-free voice chat (js/llm/voiceModes.js): listens even with the chat closed and
    // sends every utterance as a turn — the toolbar mic / Alt+M as a get/set toggle.
    // Turning it on stops any composer dictation; throws where the browser cannot listen.
    get voiceChat() { return !!app.voice?.voiceChat; },
    set voiceChat(on) {
      if (!app.voice) throw new Error('Voice input not ready — the editor UI has not wired yet');
      app.voice.voiceChat = !!on;
    },
    clearLines() { app.clearAllLines(); return stencil; },
    // variant: 'current' (default — tint + lines/points) | 'original' (no tint, no
    // lines/points) | 'tint' (tint only, no lines/points) | 'split' (download only,
    // needs a split compare view — the composite WITH the divider baked in).
    downloadImage(variant = 'current') { app.export.saveImage(variant); return stencil; },
    copyLayout() { app.export.copyLayoutToClipboard(); return stencil; },
    // variant: 'current' | 'original' | 'tint' — see downloadImage. 'current' during a
    // split compare view copies the split composite shown on screen (no divider).
    copyImage(variant = 'current') { app.export.copyImageToClipboard(variant); return stencil; }, // alias of copyImageToClipboard
    copyImageToClipboard(variant = 'current') { app.export.copyImageToClipboard(variant); return stencil; },
    shareImage() { app.export.shareImage(); return stencil; },          // Web Share API (mobile/PWA)
    openIn() { document.getElementById('open-in-btn')?.click(); return stencil; },   // Open-in-another-app modal
    downloadLayout() { app.export.downloadJSON(); return stencil; },
    get layout() { return stencil.current?.layout; },
    // Accepts a layout OBJECT or a raw JSON string. Routes through the clipboard-paste
    // path, so an existing layout raises the Combine / Replace / Cancel prompt.
    set layout(data) { app.export.applyPastedLayout(toLayoutObject(data)); },
    // Apply a layout without any prompt or toast. `data` is an object or a JSON string;
    // `mode:'combine'` keeps the current lines and adds these on top (default replaces),
    // and `history:false` keeps it out of undo.
    //   stencil.applyLayout('{"lines":[…]}', { mode: 'combine' })
    applyLayout(data, opts = {}) {
      app.export.installLayout(toLayoutObject(data), opts);
      return stencil;
    },
    // Install lines directly. Unlike `stencil.layout = …` (the clipboard-paste path)
    // this raises no "Replace layout?" prompt and shows no toast, so it suits code that
    // is putting a known set of lines in place. `history:false` keeps it out of undo.
    //   stencil.setLines([{ points: [{x:0,y:0},{x:10,y:10}], color: '#f00' }])
    setLines(lines, opts = {}) {
      const size = stencil.imageSize;
      const list = Array.isArray(lines) ? lines : [];
      app.export.installLayout(
        size ? { imageWidth: size.width, imageHeight: size.height, lines: list } : { lines: list },
        opts);
      return stencil;
    },

    // Save the whole project as a portable .stencil file (image + layout + metadata + optional
    // theme; `opts.includeTheme` default true embeds light/dark + accent). Resolves to the facade.
    saveProjectFile(opts = {}) { return app.export.saveProjectFile(opts).then(() => stencil); },
    // Open a .stencil project. Pass a File or the raw JSON text; omit to show a file picker.
    // Loads it as a fresh local project. Resolves to the facade.
    openProjectFile(fileOrText) {
      const p = fileOrText == null ? app.export.pickAndOpenProjectFile() : app.export.openProjectFile(fileOrText);
      return p.then(() => stencil);
    },
    // Live two-way sync of a file-linked project to its .stencil (Chromium only): toggle `liveSync`,
    // read `linkedFile` (name or null), `syncNow()` flushes a pending auto-save.
    get liveSync() { return app.stencilSync.supported && app.stencilSync.liveSync; },
    set liveSync(on) { app.stencilSync.liveSync = !!on; },
    get linkedFile() { return app.stencilSync.linked ? app.stencilSync.name : null; },
    syncNow() { return app.stencilSync.flush().then(() => stencil); },
    // Delete the linked .stencil file from disk (Chromium only); confirms first, then unlinks so
    // live-sync stops. The project stays open in the editor. Resolves to the facade.
    deleteProjectFile() { return app.export.deleteProjectFile().then(() => stencil); },

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
    // `opts.address` (a connected server URL) also creates+links an empty project on that
    // server, so a later save() writes back; resolves to the facade in that case.
    newEditor(opts = {}) {
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before resetting
      app.newEditor();
      if (address) return app.createRemoteBlank(address).then(() => stencil);
      return stencil;
    },
    // Create a solid-color blank image to draw on. `color` is any CSS color (default
    // white); opts.size = { width, height } px (defaults to the current page size);
    // `opts.address` also creates+links the project on that connected server.
    async blank(color = '#ffffff', opts = {}) {
      const size = opts.size || {};
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before replacing
      const blankOpts = { color, width: size.width, height: size.height };
      if (address) blankOpts.address = address;
      await app.createBlankImage(blankOpts);   // awaited: the swap is already done
      await waitForImage();
      return stencil;
    },
    // Save the session: a server-linked project writes its layout + result back to
    // its origin server (version-guarded); a purely-local one just flushes to storage.
    // Resolves to the facade.
    save() {
      if (app.remoteLink) return app.remoteSync.saveToServer().then(() => stencil);
      app.storage.save();
      return stencil;
    },

    // Crop by axis edges. Each of x1/y1/x2/y2 may be a number (px move of that edge),
    // an absolute length ('3cm'/'-4in'/'50%'/'-60%'; '-' = from the axis end), or omitted
    // (keep current edge). Commits via the same applyCrop the UI uses.
    // Proportion fill: when exactly ONE axis is given and the other is fully omitted, the
    // missing axis's LENGTH is derived from the page proportion (matching page width:height)
    // rather than kept; it keeps its current start edge. `album` (default false) picks the
    // orientation — false (portrait): height = width × (long/short side), width = height ÷ (…);
    // album true (landscape) is the inverse. Giving both axes (or neither) stays free-form.
    // `aspect` ('W:H', positive integers) then fits the resolved rect to that ratio by
    // SHRINKING one dimension symmetrically about its centre (never grows; degenerate
    // results keep at least 1px) — alone it applies to the current full rect.
    // Alternatively, `{ scale }` grows (>1) / shrinks (<1) the current crop about its centre
    // (aspect + centre kept, clamped), matching the modal's wheel/pinch — mutually exclusive
    // with the edge tokens above.
    crop(spec = {}) {
      if (!app.originalImage) throw new Error('No image loaded to crop');
      const dims = app.imageModel.effectiveOriginalDims();   // { w, h } in rotated-original pixels
      const r = app.cropRect || app.imageModel.defaultCropRect();
      if (spec.scale != null) {
        const factor = Number(spec.scale);
        if (!(factor > 0)) throw new Error('crop scale must be a positive number');
        const aspect = r.height > 0 ? r.width / r.height : 1;
        const next = scaleCropCentered(r, factor, aspect, dims.w, dims.h);
        app.imageModel.applyCrop({ x: next.x, y: next.y, width: next.width, height: next.height }, { recalc: true });
        return stencil;
      }
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
      let rx = Math.min(x1, x2), ry = Math.min(y1, y2);
      let rw = Math.abs(x2 - x1), rh = Math.abs(y2 - y1);
      // Optional aspect fit (mirrors core resolveCropRect): shrink ONE dimension
      // symmetrically about the centre to hit W:H — never grow, never move the rect.
      if (spec.aspect != null) {
        const m = /^(\d+):(\d+)$/.exec(String(spec.aspect));
        const ratio = m && Number(m[1]) > 0 && Number(m[2]) > 0 ? Number(m[1]) / Number(m[2]) : 0;
        if (!(ratio > 0)) throw new Error('crop aspect must be "W:H" with positive integers');
        let w = rw, h = rh;
        if (h * ratio <= w) w = h * ratio;   // too wide → shrink the width
        else h = w / ratio;                  // too tall → shrink the height
        // Degenerate results keep at least 1px, but never grow past the resolved rect.
        w = Math.min(rw, Math.max(w, 1));
        h = Math.min(rh, Math.max(h, 1));
        rx += (rw - w) / 2; ry += (rh - h) / 2;
        rw = w; rh = h;
      }
      app.imageModel.applyCrop({ x: rx, y: ry, width: rw, height: rh }, { recalc: true });
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
        'unit', 'lineColor', 'pointColor', 'pointSize', 'thickness', 'lineStyle',
        'pointStyle', 'showPoints', 'showLines', 'filter', 'filterColor', 'pageSize', 'drawMode',
        'allowFormulas', 'formulaX', 'formulaY', 'fillColor', 'selectionGlow', 'hoverRing', 'focusRing',
      ]) {
        if (opts[k] != null) set[k] = opts[k];
      }
      if (opts.page != null) set.pageSize = opts.page;            // `page` alias for pageSize
      if (opts.showTooltip != null) app.settings.setTooltipOption('enabled', opts.showTooltip);
      if (opts.tooltip && typeof opts.tooltip === 'object')
        for (const k of ['enabled', 'page', 'screen', 'coords'])
          if (opts.tooltip[k] != null) app.settings.setTooltipOption(k, opts.tooltip[k]);
      if (opts.fullscreen != null) stencil.fullscreen = opts.fullscreen;
      if (opts.incognito != null) stencil.incognito = opts.incognito;
      if (opts.zoom != null) stencil.zoom(opts.zoom);
      if (opts.layout != null) stencil.layout = opts.layout;
      if (opts.crop && typeof opts.crop === 'object') stencil.crop(opts.crop);
      if (opts.move && typeof opts.move === 'object') stencil.move(opts.move);
      return stencil;
    },

    // Load an image (or, with `frame`/a video URL, the frame at `frame` seconds) by URL.
    // Resolves to the facade so callers can `(await stencil.load(url)).crop(...)`.
    // `incognito: true` adopts incognito IN PLACE first: the outgoing project is flushed
    // and the URL lands in a fresh never-saved session — the tab stays put.
    async load(url, opts = {}) {
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before fetching
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
      if (address) loadOpts.address = address;   // create+link on that server after load
      const previous = app.image;
      // Adopt only once the bytes are in hand: a failed fetch must leave the editor
      // exactly as it was, never reset into an empty incognito session.
      if (opts.incognito) app.adoptIncognitoHere();
      app.loadImageFromFile(file, loadOpts);
      await waitForImage(8000, previous);
      return stencil;
    },
  };

  // Flatten the settings accessors onto the facade so `stencil.lineColor`/`showPoints`/
  // `pageSize`/`darkTheme`/… work as well as `stencil.settings.<key>` (same app setters).
  // Copies the get/set descriptors (not values); keys don't collide with facade members.
  Object.defineProperties(stencil, Object.getOwnPropertyDescriptors(settingsAccessors()));

  // Hide every member from enumeration so `console.log(stencil)`/Object.keys read clean,
  // not the whole method surface. Access + DevTools autocomplete unaffected (non-enumerable
  // ≠ inaccessible). Runs before the freeze (freeze locks descriptors).
  for (const k of Reflect.ownKeys(stencil)) {
    const d = Object.getOwnPropertyDescriptor(stencil, k);
    if (d.enumerable) Object.defineProperty(stencil, k, { ...d, enumerable: false });
  }
  // Freeze + hard-guard via the same proxy as every nested object (writing a method or
  // read-only getter THROWS; real setters still write). Tamper-resistance, not security.
  // Reassigning the closure ref makes every `return stencil` hand back the guarded proxy.
  stencil = guard(stencil);
  return stencil;
};
