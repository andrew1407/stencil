import { notify } from '../../utils.js';
import { adoptServerFilter, adoptServerPageFormat, adoptServerFormulas } from '../../ui/canvas/serverLayoutPaint.js';
import { getSyncToServer } from '../../net/connectionStore.js';
import { guardedFetch } from '../../net/fetchGuard.js';
import { shouldReloadFromEvent } from '../../net/remoteSync.js';
import { ResultUploader } from './resultUpload.js';
import { imageSignature, withOriginal, samePicture, applyPeerLayout } from './peerLayout.js';
import { pushLayout, putResult, captureResult } from './push.js';
import { restingJob } from '../draw/restingPaint.js';
import { resultPngBytes } from '../../worker/imageTasks.js';
import { keptSource } from '../project/store/projectSources.js';
import constants from '../../config/constants.json' with { type: 'json' };
import { loadImageFromFile } from '../image/loadFlow.js';
import { newEditor } from '../launch/openFlow.js';

// Live co-edit push/pull + server writes: the debounced layout push, the throttled result upload,
// the peer-event pull and the conflict-merge retry loop. The link and the model live on the app.
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
  // One server write at a time, so a result upload never races a layout push into a 409 with itself.
  #writes = Promise.resolve();
  #results;
  // The picture on screen as the server named it (imageSignature); '' until one is loaded.
  #imageSig = '';
  // The state the last push / pull toast announced: a steady session toasts on a change only.
  #announced = { push: '', pull: '' };

  constructor(app) {
    this.app = app;
    const { resultIdleMs: idleMs, resultMinGapMs: minGapMs } = constants.COEDIT;
    this.#results = new ResultUploader(() => { this.#enqueue(() => this.#putResult()); }, { idleMs, minGapMs });
  }

  #enqueue(op) {
    const run = this.#writes.then(op, op);
    this.#writes = run.catch(() => {});
    return run;
  }

  #toast(channel, state, msg, kind, always = false) {
    if (always || state !== this.#announced[channel]) notify(msg, kind);
    this.#announced[channel] = state;
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
      this.#enqueue(() => this.#pushLayout(false)).then((link) => { if (link) this.#results.markDirty(); });
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
      this.#results.cancel();
      newEditor(app);
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

  // The server record of the picture just loaded, so a later peer event can tell a layout
  // edit (adopted in place) from a new picture (reloaded).
  noteServerImage(rec) { this.#imageSig = imageSignature(rec); }

  // This editor's own upload is now the server's original ('' = its hash is unknown).
  noteOwnOriginal(hash) { this.#imageSig = withOriginal(this.#imageSig, hash); }

  // The same original (equal originalHash): the layout in place, crop and turn too, history kept.
  // Otherwise the original/source is the base + the stored layout re-applied — never the baked `result`.
  async reloadRemoteActive() {
    const app = this.app;
    const link = app.remoteLink;
    if (!link || this.#reloadingRemote) return;
    const conn = app.connections && app.connections.get(link.address);
    if (!conn) return;
    this.#reloadingRemote = true;
    try {
      const full = await conn.getProject(link.remoteId);
      if (app.image && samePicture(this.#imageSig, full.project) && applyPeerLayout(app, full.layout, this)) {
        app.remoteLink = { ...link, version: full.project?.version ?? link.version };
      } else if (!(await this.#reloadPicture(conn, link, full))) return;
      this.#imageSig = imageSignature(full.project);
      this.#adoptPeerName(full.project?.name);
      this.#toast('pull', 'ok', 'Updated from server', 'ok');
    } catch { this.#toast('pull', 'failed', "Couldn't refresh from server — showing the last loaded version", 'info'); }
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

  async #reloadPicture(conn, link, full) {
    const app = this.app;
    const src = full.project?.source || '';
    const blob = await this.fetchRemoteOriginal(conn, link.remoteId, src);
    if (!blob) return false;
    const ext = blob.type?.split('/')[1] || 'png';
    const file = new File([blob], `${app.imageBaseName || 'image'}.${ext}`, { type: blob.type || 'image/png' });
    loadImageFromFile(app, file, {
      source: keptSource(src, app.imageSource),
      resource: full.project?.resource || '',
      color: full.project?.color || '',
      address: link.address,
      remoteId: link.remoteId,
      version: full.project?.version || link.version,
      layout: full.layout,
    });
    return true;
  }

  // A peer's RENAME is adopted through the store directly, so it is not echoed back.
  #adoptPeerName(peerName) {
    const app = this.app;
    if (!peerName || app.activeProjectId == null) return;
    if (peerName === app.storage.store.getMeta(app.activeProjectId)?.name) return;
    app.storage.store.rename(app.activeProjectId, peerName);
    app.imageBaseName = peerName;
    app.updateProjectTitle();
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

  // The editor state a server layout names, and the controls that show it (ui/canvas/serverLayoutPaint.js).
  adoptServerFilter(layout) { adoptServerFilter(this.app, layout); }

  adoptServerPageFormat(layout) { adoptServerPageFormat(this.app, layout); }

  adoptServerFormulas(layout) { adoptServerFormulas(this.app, layout); }

  // An explicit save (stencil.save, a download, a replaced original): the layout and the result
  // now, toasted every time. The co-edit debounce pushes the layout alone (scheduleRemoteSync).
  saveToServer() {
    return this.#enqueue(async () => {
      const link = await this.#pushLayout(true);
      if (!link) return null;
      this.#results.cancel();
      await this.#putResult();
      return this.app.remoteLink;
    });
  }

  // Leaving the project (switch, close, unload): the pending result goes up with what is on screen now.
  flushResult() {
    if (!this.#results.pending) return;
    this.#results.cancel();
    const snap = captureResult(this.app);
    this.#enqueue(() => this.#putResult(snap));
  }

  #hooks(explicit = false) {
    return {
      toast: (state, msg, kind) => this.#toast('push', state, msg, kind, explicit),
      saved: () => { this.#lastRemoteSaveAt = Date.now(); },
      adoptServerFilter: (layout) => this.adoptServerFilter(layout),
      reload: () => this.reloadRemoteActive(),
    };
  }

  #pushLayout(explicit) { return pushLayout(this.app, this.#hooks(explicit)); }

  #putResult(snap = captureResult(this.app)) { return putResult(this.app, snap, this.#hooks()); }

  // The annotated result as PNG bytes (the server's `result` blob), or null if no image.
  async renderResultBytes() {
    const app = this.app;
    if (!app.image) return null;
    return resultPngBytes(restingJob(app));
  }
}
