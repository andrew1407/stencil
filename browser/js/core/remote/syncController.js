import { notify } from '../../utils.js';
import { adoptServerFilter, adoptServerPageFormat, adoptServerFormulas } from '../../ui/canvas/serverLayoutPaint.js';
import { mergeLines } from '../layout.js';
import { getSyncToServer } from '../../net/connectionStore.js';
import { guardedFetch } from '../../net/fetchGuard.js';
import { requireConnection, saveRemoteProject, shouldReloadFromEvent } from '../../net/remoteSync.js';

// Live co-edit push/pull + server writes: the debounced save-back, the peer-event reload,
// the conflict-merge retry loop. `remoteLink`, connections and the editor model live on the app.
export class RemoteSyncController {
  // Debounce timer + burst start (max-wait cap) for the trailing save-back.
  #syncTimer = null;
  #syncFirstAt = 0;
  // Our last server save, to ignore the server's echo of our own change.
  #lastRemoteSaveAt = 0;
  // Guard while a reload is applying, plus the two "landed mid-reload" deferrals.
  #reloadingRemote = false;
  #syncPending = false;   // a push deferred during a reload, flushed when it settles
  #reloadPending = false; // a peer change that landed mid-reload, applied when it settles

  constructor(app) {
    this.app = app;
  }

  // Debounce a save-back after an edit so peers get a `project-event`. No-op for local-only
  // projects and when "Sync changes to server" is off.
  scheduleRemoteSync() {
    const app = this.app;
    if (!app.remoteLink || !getSyncToServer()) return;
    // Mid-reload: defer (don't drop); reloadRemoteActive flushes it when it settles.
    if (this.#reloadingRemote) { this.#syncPending = true; return; }
    // Trailing debounce capped by a max-wait, so CONTINUOUS editing still flushes every ~1.5s.
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

  // A server project-event for the project we're editing (wired from stencilApi onChange).
  onServerProjectEvent(msg, conn) {
    const app = this.app;
    // The linked project was deleted: detach fully, or the golden server cues outlive it.
    // Independent of the sync toggle.
    if (msg?.type === 'project-event' && msg.event === 'deleted' && app.remoteLink
        && msg.project?.id === app.remoteLink.remoteId
        && (!conn || conn.url === app.remoteLink.address)) {
      app.newEditor();
      app.updateButtons?.();
      notify('This server project was deleted', 'info');
      return;
    }
    if (!getSyncToServer()) return;
    if (!shouldReloadFromEvent(msg, app.remoteLink, {
      lastLocalSaveAt: this.#lastRemoteSaveAt,
      isDrawing: app.isDrawing,
      connUrl: conn ? conn.url : null,
    })) return;
    // Mid-reload: queue a single follow-up pass (collapse to latest).
    if (this.#reloadingRemote) { this.#reloadPending = true; return; }
    this.reloadRemoteActive();
  }

  // Original/source is the base + the stored layout re-applied — never the baked `result`,
  // which would double-draw. Guarded so the reload's own redraws don't push back.
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
      // A peer's RENAME is adopted through the store directly, so it is not echoed back.
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
        // A local edit landed mid-reload → flush it (our push supersedes; the server's
        // last-writer-wins resolves it) and drop any queued reload. Else apply one more pass.
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

  // The server's original bytes, else its http(s) source URL (CORS). Blob or null.
  async fetchRemoteOriginal(conn, remoteId, src) {
    let blob = null;
    try { blob = await conn.fetchFile(remoteId, 'original'); } catch {}
    if (!blob && /^https?:/i.test(src || '')) {
      const resp = await guardedFetch(src, { mode: 'cors' });
      if (resp.ok) blob = await resp.blob();
    }
    return blob;
  }

  // The editor state a server layout names, and the controls that show it (ui/serverLayoutPaint.js).
  adoptServerFilter(layout) { adoptServerFilter(this.app, layout); }

  adoptServerPageFormat(layout) { adoptServerPageFormat(this.app, layout); }

  adoptServerFormulas(layout) { adoptServerFormulas(this.app, layout); }

  // Save the annotated result + layout back. On a 409, pull, union-merge the peer's lines
  // with ours and retry until convergence (the result-upload adds an extra bump).
  async saveToServer() {
    const app = this.app;
    if (!app.remoteLink) return null;
    if (!getSyncToServer()) return null;
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
      this.#lastRemoteSaveAt = Date.now();
      try {
        app.remoteLink = await saveRemoteProject(conn, app.remoteLink, {
          name, layout, bytes, ext: 'png', w: app.canvas.width, h: app.canvas.height,
        });
        this.#lastRemoteSaveAt = Date.now();
        app.filterDirty = false;
        notify(attempt === 0 ? 'Saved to server' : 'Merged changes from another editor', 'ok');
        return app.remoteLink;
      } catch (err) {
        if (!err || !err.conflict) {
          notify(`Server save failed — ${err.message}`, 'fail');
          return null;
        }
        // A peer saved first: merge their lines, adopt the server version, loop — re-merging
        // each time so repeated bumps can't drop our edit.
        try {
          const full = await conn.getProject(app.remoteLink.remoteId);
          app.remoteLink = { ...app.remoteLink, version: full.project?.version ?? app.remoteLink.version };
          const sl = full.layout || {};
          const serverLines = Array.isArray(sl.lines) ? sl.lines : [];
          app.lines = mergeLines(serverLines, app.lines);
          // A line-only edit must not clobber a peer's filter change (the scalar can't merge).
          if (!app.filterDirty) this.adoptServerFilter(sl);
          app.history.push(app.lines);
          app.renderer.redraw();
        } catch { /* fetch failed; loop retries with current state */ }
      }
    }
    // Reload so the user sees a consistent state (our lines were merged in by an earlier pass).
    notify('Sync conflict — reloaded latest from the server', 'info');
    this.reloadRemoteActive();
    return null;
  }

  // The annotated result as PNG bytes (the server's `result` blob), or null if no image.
  async renderResultBytes() {
    const app = this.app;
    if (!app.image) return null;
    const off = app.renderResultCanvas();
    const blob = await new Promise(res => off.toBlob(res, 'image/png'));
    return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
  }
}
