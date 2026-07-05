import { notify, setVal, setRadioGroup, cmToUnit } from '../utils.js';
import { normalizePageSize } from './units.js';
import { mergeLines } from './layout.js';
import { getSyncToServer } from '../net/connectionStore.js';
import { requireConnection, saveRemoteProject, shouldReloadFromEvent } from '../net/remoteSync.js';

// ── RemoteSyncController: live co-edit push/pull + server writes ─────
// Extracted from drawingApp.js. Owns the debounced save-back, the peer-event reload, the
// conflict-merge retry loop, and the server layout-adoption helpers. Holds the sync timing
// state itself (the timers/flags that only this engine touches); everything else (the session
// link `remoteLink`, connections, the editor model) lives on the back-referenced app. The
// project transfer/create helpers that are entangled with loadImageFromFile stay on DrawingApp
// and call the now-public helpers here (adoptServer*/fetchRemoteOriginal/renderResultBytes).
export class RemoteSyncController {
  // Debounce timer + burst start (max-wait cap) for the trailing save-back.
  #syncTimer = null;
  #syncFirstAt = 0;
  // Timestamp of our last server save, to ignore the server's echo of our own change.
  #lastRemoteSaveAt = 0;
  // Guard while a reload is applying, plus the two "landed mid-reload" deferrals.
  #reloadingRemote = false;
  #syncPending = false;   // a push deferred during a reload, flushed when it settles
  #reloadPending = false; // a peer change that landed mid-reload, applied when it settles

  constructor(app) {
    this.app = app;
  }

  // Live co-edit (send): debounce a save-back to the server after an edit so peers on
  // the same project get a `project-event` and reload. No-op for local-only projects,
  // and when the "Sync changes to server" setting is off (edit-in-memory only).
  scheduleRemoteSync() {
    const app = this.app;
    if (!app.remoteLink || !getSyncToServer()) return;
    // Mid-reload: defer (don't drop) the push; reloadRemoteActive flushes it when it settles.
    if (this.#reloadingRemote) { this.#syncPending = true; return; }
    // Trailing debounce (coalesce a burst of edits into one save) but capped by a max-wait so
    // CONTINUOUS editing still flushes to peers every ~1.5s instead of starving until a pause.
    const now = Date.now();
    if (!this.#syncFirstAt) this.#syncFirstAt = now;
    const wait = Math.max(0, Math.min(350, 1500 - (now - this.#syncFirstAt)));
    clearTimeout(this.#syncTimer);
    this.#syncTimer = setTimeout(() => {
      this.#syncFirstAt = 0;
      if (!app.remoteLink || !getSyncToServer()) return;
      if (this.#reloadingRemote) { this.#syncPending = true; return; }
      this.saveToServer();
    }, wait);
  }

  // Live co-edit (receive): a server project-event for the project we're editing —
  // reload from the server when it's a genuine peer change. Wired from the connection
  // event feed (stencilApi onChange).
  onServerProjectEvent(msg, conn) {
    const app = this.app;
    if (!getSyncToServer()) return; // sync off — don't auto-pull peer changes
    if (!shouldReloadFromEvent(msg, app.remoteLink, {
      lastLocalSaveAt: this.#lastRemoteSaveAt,
      isDrawing: app.isDrawing,
      connUrl: conn ? conn.url : null,
    })) return;
    // Mid-reload: another peer change arrived while we're applying one. Queue a single
    // follow-up pass (collapse to latest) rather than dropping it; the settle block runs it.
    if (this.#reloadingRemote) { this.#reloadPending = true; return; }
    this.reloadRemoteActive();
  }

  // Re-fetch the active server project (image + layout) and apply it, so a peer's saved
  // change shows live. Uses original/source as the base + re-applies the stored layout
  // (lines + filter) — never the baked `result` (would double-draw). Guarded so the
  // reload's own redraws don't trigger a push back.
  async reloadRemoteActive() {
    const app = this.app;
    const link = app.remoteLink;
    if (!link || this.#reloadingRemote) return;
    const conn = app.connections && app.connections.get(link.address);
    if (!conn) return;
    this.#reloadingRemote = true;
    try {
      const full = await conn.getProject(link.remoteId);
      const src = full.project?.source || '';
      const blob = await this.fetchRemoteOriginal(conn, link.remoteId, src);
      if (!blob) return;
      const ext = (blob.type && blob.type.split('/')[1]) || 'png';
      const file = new File([blob], `${app.imageBaseName || 'image'}.${ext}`, { type: blob.type || 'image/png' });
      app.loadImageFromFile(file, {
        source: src,
        resource: full.project?.resource || '',
        color: full.project?.color || '',
        address: link.address,
        remoteId: link.remoteId,
        version: full.project?.version || link.version,
        layout: full.layout,
      });
      // Adopt a peer's RENAME too (local-only — the server already holds it; use the store
      // directly, not renameProject, so we don't echo the change back to the server).
      const peerName = full.project?.name;
      if (peerName && app.activeProjectId != null &&
          peerName !== app.storage.store.getMeta(app.activeProjectId)?.name) {
        app.storage.store.rename(app.activeProjectId, peerName);
        app.imageBaseName = peerName;
        app.updateProjectTitle();
      }
      notify('Updated from server', 'ok');
    } catch { notify("Couldn't refresh from server — showing the last loaded version", 'info'); }
    finally {
      setTimeout(() => {
        this.#reloadingRemote = false;
        // A local edit landed during the reload → flush it (our push supersedes; the server's
        // last-writer-wins resolves it), and drop any queued reload since our save will
        // re-broadcast the merged state. Otherwise, if a peer change queued mid-reload,
        // apply one more pass so we converge to the latest.
        if (this.#syncPending) {
          this.#syncPending = false;
          this.#reloadPending = false;
          this.scheduleRemoteSync();
        } else if (this.#reloadPending) {
          this.#reloadPending = false;
          this.reloadRemoteActive();
        }
      }, 120);
    }
  }

  // Fetch a server project's original bytes, falling back to its http(s) source URL
  // (CORS) when the server stores none. Returns a Blob or null.
  async fetchRemoteOriginal(conn, remoteId, src) {
    let blob = null;
    try { blob = await conn.fetchFile(remoteId, 'original'); } catch {}
    if (!blob && /^https?:/i.test(src || '')) {
      const resp = await fetch(src, { mode: 'cors' });
      if (resp.ok) blob = await resp.blob();
    }
    return blob;
  }

  // Adopt a server layout's filter/tint into the editor + filter UI, and clear the
  // dirty flag (the server's filter is now ours). Used by conflict-merge so a line-only
  // edit preserves a peer's filter change instead of clobbering it.
  adoptServerFilter(layout) {
    const app = this.app;
    if (!layout) return;
    app.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
    if (layout.filterColor) app.filterColor = layout.filterColor;
    setVal('image-filter', app.imageFilter);
    setRadioGroup('ctxFilter', app.imageFilter);
    const fp = document.getElementById('filter-color');
    if (fp) {
      fp.value = app.filterColor;
      fp.style.display = app.imageFilter === 'custom' ? 'inline-block' : 'none';
    }
    app.filterDirty = false;
  }

  // Restore a server layout's page format (A3/A4/custom + cm dims) into state + the page UI.
  adoptServerPageFormat(layout) {
    const app = this.app;
    if (!layout) return;
    const n = normalizePageSize(layout.pageSize);
    if (n) {
      app.pageSize = n;
      setVal('page-size', n);
      const cg = document.getElementById('custom-size-group');
      if (cg) cg.style.display = n === 'custom' ? 'inline-flex' : 'none';
    }
    if (Number.isFinite(layout.customPageWidth)) {
      app.customPageWidth = layout.customPageWidth;
      setVal('custom-page-width', cmToUnit(layout.customPageWidth, app.unit));
    }
    if (Number.isFinite(layout.customPageHeight)) {
      app.customPageHeight = layout.customPageHeight;
      setVal('custom-page-height', cmToUnit(layout.customPageHeight, app.unit));
    }
  }

  // Restore a server layout's x/y formulas into state + the formula UI. The expressions are
  // kept regardless of the toggle (allow only gates visibility + whether they're applied).
  adoptServerFormulas(layout) {
    const app = this.app;
    const allow = !!(layout && layout.allowFormulas);
    app.allowFormulas = allow;
    const cb = document.getElementById('allow-formulas');
    if (cb) cb.checked = allow;
    app.settings.syncFormulaUI(allow);
    const fx = layout && typeof layout.formulaX === 'string' ? layout.formulaX : '';
    const fy = layout && typeof layout.formulaY === 'string' ? layout.formulaY : '';
    app.formulaX = fx;
    app.formulaY = fy;
    setVal('formula-x', app.formulaX);
    setVal('formula-y', app.formulaY);
    setVal('ctx-formula-x', app.formulaX);
    setVal('ctx-formula-y', app.formulaY);
    app.settings.showFormulaError(false);
  }

  // Save the current annotated result + layout back to the linked server project.
  // On a 409 version conflict, pull latest, union-merge the peer's lines with ours, and
  // retry until convergence (handles the result-upload's extra bump). No-op when local-only.
  async saveToServer() {
    const app = this.app;
    if (!app.remoteLink) return null;
    if (!getSyncToServer()) return null; // sync off — fetched project stays edit-in-memory only
    let conn;
    try {
      conn = requireConnection(app.connections, app.remoteLink.address);
    } catch (err) {
      notify(err.message, 'fail');
      return null;
    }
    const name = app.activeProjectId != null
      ? (app.storage.store.getMeta(app.activeProjectId)?.name || app.imageBaseName || 'Untitled')
      : (app.imageBaseName || 'Untitled');
    const MAX_TRIES = 6;
    for (let attempt = 0; attempt < MAX_TRIES; attempt++) {
      const layout = app.currentLayoutPayload();
      const bytes = await this.renderResultBytes();
      this.#lastRemoteSaveAt = Date.now();   // open the echo-suppression window
      try {
        app.remoteLink = await saveRemoteProject(conn, app.remoteLink, {
          name, layout, bytes, ext: 'png', w: app.canvas.width, h: app.canvas.height,
        });
        this.#lastRemoteSaveAt = Date.now();
        app.filterDirty = false;   // our filter (if any) is now the server's
        notify(attempt === 0 ? 'Saved to server' : 'Merged changes from another editor', 'ok');
        return app.remoteLink;
      } catch (err) {
        if (!err || !err.conflict) {
          notify(`Server save failed — ${err.message}`, 'fail');
          return null;
        }
        // Conflict: a peer saved first. Merge their latest lines into ours, adopt the
        // server version, and loop to retry — re-merging each time so repeated bumps
        // (the unguarded result upload) can't drop our edit.
        try {
          const full = await conn.getProject(app.remoteLink.remoteId);
          app.remoteLink = { ...app.remoteLink, version: full.project?.version ?? app.remoteLink.version };
          const sl = full.layout || {};
          const serverLines = Array.isArray(sl.lines) ? sl.lines : [];
          app.lines = mergeLines(serverLines, app.lines);
          // Adopt the peer's filter UNLESS this user just changed their own — so a
          // line-only edit doesn't clobber a peer's filter change (the scalar can't merge).
          if (!app.filterDirty) this.adoptServerFilter(sl);
          app.history.push(app.lines);
          app.renderer.redraw();
        } catch { /* fetch failed; loop retries with current state */ }
      }
    }
    // Couldn't win the race after several merges — reload so the user sees a consistent
    // state (our pending lines were already merged into the server's by an earlier pass).
    notify('Sync conflict — reloaded latest from the server', 'info');
    this.reloadRemoteActive();
    return null;
  }

  // Render the annotated result to PNG bytes (the server's `result` blob), or null if no image.
  async renderResultBytes() {
    const app = this.app;
    if (!app.image) return null;
    const off = app.renderResultCanvas();
    const blob = await new Promise(res => off.toBlob(res, 'image/png'));
    return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
  }
}
