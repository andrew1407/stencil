// ── window.stencil: the Project wrapper ─────────────────────────
// Extracted from stencilApi.js. Wraps one registry row (or this tab's incognito editor)
// and routes every mutation through the same app methods the projects modal uses.
import { PROJECT_ACTION } from '../worker/messages.js';
import { PERIOD_ORDER, DEFAULT_PERIOD } from '../core/project/store/projectsStore.js';
import { parseDuration } from '../core/parse/durationParser.js';
import { normalizeHex } from '../core/settings/accents.js';
import { publish, EVENTS } from '../eventBus/appBus.js';
import { splitKeywords, str } from './coerce.js';

export const DURATION_HELP = [
  'stencil.expire(spec) — set when the active project expires, from a duration:',
  "  a unit alone (one of it):  'day' · 'week' · 'fortnight' · 'month' · 'year'",
  "  a count + unit (either order):  'days 23' · 'months 3' · '3 weeks'",
  "  keep forever:  'off' · 'never' · 'none'",
  'Called with no argument this prints these formats; with one it applies the expiry.',
].join('\n');

export const createProjectWrapper = ({ app, guard, openedIds }) => {
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
        publish(EVENTS.registryChanged);
        return;
      }
      const proj = store().get(id);
      if (!proj) throw new Error(`Unknown project ${id}`);
      proj.meta[metaKey] = val;
      proj.payload.layout[appKey] = val;
      store().upsert(proj.meta, proj.payload);
      app.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    };
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
      // The project's description, as the Description window edits it. '' clears it.
      get description() { return incognito ? '' : (meta()?.description ?? ''); },
      set description(v) {
        if (incognito) throw new Error('Cannot describe an incognito editor');
        if (app.setProjectDescription(id, str(v)) == null) throw new Error(`Could not set description on project ${id}`);
      },
      get keywords() { return incognito ? [] : (meta()?.keywords ?? []).slice(); },
      set keywords(v) {
        if (incognito) throw new Error('Cannot set keywords on an incognito editor');
        if (app.setProjectKeywords(id, splitKeywords(v)) == null) throw new Error(`Could not set keywords on project ${id}`);
      },
      // Whether this is a blank-image project (solid-colour background). Read-only.
      get blank() { return incognito ? false : !!meta()?.blank; },
      // True when this project was opened from a portable .stencil file (drives the bronze
      // projects-list outline / badge). Read-only provenance flag.
      get fromFile() { return incognito ? false : !!meta()?.fromFile; },
      // Setting a blank colour on a non-blank project throws: only blanks have one.
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
        if (app.setProjectKeywords(id, [...(meta()?.keywords ?? []), ...splitKeywords(kw)]) == null) throw new Error(`Could not add keywords on project ${id}`);
        return project;
      },
      removeKeywords(...kw) {
        if (incognito) throw new Error('Cannot set keywords on an incognito editor');
        const drop = new Set(splitKeywords(kw).map((k) => k.toLowerCase()));
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

  return makeProject;
};
