// window.stencil — the chainable console control API over the live DrawingApp.
// NEVER reimplements editor behaviour: every mutation routes through the same core
// methods the toolbar uses, so console and toolbar stay in sync.
// A closure factory, not a class — `app` lives in scope and the returned objects carry
// no fields. index.js builds it after the app → window.stencil.
import { hotkeys } from '../core/settings/hotkeys.js';
import { ConnectionManager } from '../net/connectionManager.js';
import { loadSavedServers, saveServers, getAutoConnect } from '../net/connectionStore.js';
import { notify } from '../utils.js';
import { publish, EVENTS } from '../eventBus/appBus.js';
import { createLineWrappers } from './lineAndPoint.js';
import { createProjectWrapper } from './project.js';
import { createSettingsFacade } from './settingsFacade.js';
import { createProjectsApi } from './api/projectsApi.js';
import { createConnectApi } from './api/connectApi.js';
import { createAssistantApi } from './api/assistantApi.js';
import { createWindowsApi } from './api/windowsApi.js';
import { createEditorActions } from './editorActions.js';
import { createExportActions } from './exportActions.js';
import { createSessionApi } from './api/sessionApi.js';
import { createCropApi } from './api/cropApi.js';
import { createScriptApi } from './api/scriptApi.js';
import { createEasterEggsApi } from './api/easterEggsApi.js';

export { WINDOWS } from './api/windowsApi.js';

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
        publish(EVENTS.connectionsChanged);
      } catch { /* no DOM */ }
    },
  }));
  // On first boot, optionally re-establish the saved server set ("auto-connect on open").
  // Each connects independently, and each unreachable address gets its own toast.
  if (firstInit && getAutoConnect()) {
    const saved = loadSavedServers();
    // A credential the server ALREADY refused is never retried. The row is adopted into
    // the expired set instead, so the Servers button keeps its dot and offers Reconnect.
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

  // Hard-guard an API object: real setters write through, but writing a method or
  // read-only getter THROWS instead of silently no-opping in the non-strict console.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // The Line / Point / Project wrappers and the settings namespace are their own modules
  // beside this one (lineAndPoint.js / project.js / settingsFacade.js).
  const { makePoint, makeLine, setFacade } = createLineWrappers({ app, guard });
  const makeProject = createProjectWrapper({ app, guard, openedIds });
  const { settingsAccessors, settings } = createSettingsFacade({ app, guard });

  // The rest of the facade lives in sibling modules, one per concern. Each returns its
  // members plus a setFacade, so their `return stencil` hands back the frozen proxy.
  const parts = [
    createProjectsApi({ app, makeProject, openedIds }),
    createConnectApi({ app, connMgr }),
    createAssistantApi({ app, guard }),
    createWindowsApi(),
    createEditorActions({ app }),
    createExportActions({ app }),
    createSessionApi({ app, connMgr }),
    createCropApi({ app }),
    createScriptApi(),
    createEasterEggsApi({ app, guard }),
  ];

  stencil = {
    // The extension's editor-page API on window.__stencilExt — the extension owns its shape
    // (browser-extension/README.md). null unless installed and enabled.
    get extension() { return (typeof window !== 'undefined' && window.__stencilExt) || null; },

    // ── Settings / modes ──
    get settings() { return settings(); },
    get fullscreen() { return typeof document !== 'undefined' && document.body.classList.contains('fullscreen-mode'); },
    set fullscreen(v) { if (!!v !== stencil.fullscreen && typeof app.toggleFullscreen === 'function') app.toggleFullscreen(); },
    get imageSize() { const img = app.image; return img ? { width: img.width, height: img.height } : undefined; },
    // Current crop rect in rotated-original px — {x,y,w,h} plus legacy width/height
    // aliases; null before an image loads. The LLM plan executor re-maps against it (§1).
    get cropRect() { const r = app.cropRect; return r ? { x: r.x, y: r.y, w: r.width, h: r.height, width: r.width, height: r.height } : null; },
    get incognito() { return !!app.storage.incognito; },
    // Incognito can only be turned on for a blank editor (no image yet) — same rule as
    // the toolbar toggle; setting it otherwise throws.
    set incognito(on) {
      if (!!app.storage.incognito === !!on) return;
      if (on && (app.image || app.lines.length || app.activeProjectId != null || !app.storage.temporary))
        throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
      // Turning it OFF with work on screen keeps the work: leaving incognito IS the user
      // asking to save, so it promotes to a local project (promoteIncognitoToLocal).
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
  };
  for (const part of parts) Object.defineProperties(stencil, Object.getOwnPropertyDescriptors(part.api));

  // Flatten the settings accessors onto the facade so `stencil.lineColor` works as well as
  // `stencil.settings.lineColor`. Copies the get/set descriptors, not values.
  Object.defineProperties(stencil, Object.getOwnPropertyDescriptors(settingsAccessors()));

  // Hide every member from enumeration so `console.log(stencil)` reads clean. Access and
  // autocomplete are unaffected; runs before the freeze, which locks descriptors.
  for (const k of Reflect.ownKeys(stencil)) {
    const d = Object.getOwnPropertyDescriptor(stencil, k);
    if (d.enumerable) Object.defineProperty(stencil, k, { ...d, enumerable: false });
  }
  // Freeze + hard-guard via the same proxy as every nested object. Tamper-resistance,
  // not security. Reassigning the ref makes every `return stencil` hand back the proxy.
  stencil = guard(stencil);
  setFacade(stencil);   // a removed Point falls back to the facade for chaining
  for (const part of parts) part.setFacade(stencil);
  return stencil;
};
