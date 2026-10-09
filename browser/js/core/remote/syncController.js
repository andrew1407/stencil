import { notify } from '../../utils.js';
import { adoptServerFilter, adoptServerPageFormat, adoptServerFormulas } from '../../ui/canvas/serverLayoutPaint.js';
import { getSyncToServer } from '../../net/connectionStore.js';
import { shouldReloadFromEvent, ECHO_WINDOW_MS } from '../../net/remoteSync.js';
import { ResultUploader } from './resultUpload.js';
import { imageSignature, withOriginal, samePicture, applyPeerLayout } from './peerLayout.js';
import { pushLayout, putResult, captureResult, sameLink } from './push.js';
import { RemotePull } from './pull.js';
import { restingJob } from '../draw/restingPaint.js';
import { resultPngBytes } from '../../worker/imageTasks.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { newEditor } from '../launch/openFlow.js';

// Live co-edit push/pull + server writes: the debounced layout push, the throttled result upload,
// the peer-event pull and the conflict-merge retry loop. The link and the model live on the app.
export class RemoteSyncController {
  // Debounce timer + burst start (max-wait cap) for the trailing save-back.
  #syncTimer = null;
  #syncFirstAt = 0;
  // Our last server save, to ignore the server's echo of our own change.
  #lastRemoteSaveAt = 0;
  #reloadingRemote = false;
  #syncPending = false;   // a push deferred during a reload, flushed when it settles
  // The newest peer record that landed mid-reload or behind a pending push: pulled once the
  // writes settle, unless the push merged past it (its 409 path adopts the peer's lines).
  #reloadPending = null;
  // One server write at a time, so a result upload never races a layout push into a 409 with itself.
  #writes = Promise.resolve();
  #busy = 0;
  #results;
  // The picture on screen as the server named it (imageSignature); '' until one is loaded.
  #imageSig = '';
  // The state the last push / pull toast announced: a steady session toasts on a change only.
  #announced = { push: '', pull: '' };
  // Bumped by detach(): a writer that started on an earlier project drops what came back.
  #epoch = 0;
  #pull;

  constructor(app) {
    this.app = app;
    this.#pull = new RemotePull(app);
    const { resultIdleMs: idleMs, resultMinGapMs: minGapMs } = constants.COEDIT;
    this.#results = new ResultUploader(() => { this.#enqueue(() => this.#putResult()); }, { idleMs, minGapMs });
  }

  #enqueue(op) {
    this.#busy++;
    const run = this.#writes.then(op, op);
    this.#writes = run.catch(() => {}).then(() => { this.#busy--; this.#pullIfPending(); });
    return run;
  }

  #toast(channel, state, msg, kind, always = false) {
    if (always || state !== this.#announced[channel]) notify(msg, kind);
    this.#announced[channel] = state;
  }

  // False once the editor left the project a round trip started on: detach() bumped the epoch,
  // or the link names another project.
  #liveOn(link) {
    const epoch = this.#epoch;
    return () => epoch === this.#epoch && sameLink(this.app.remoteLink, link);
  }

  // A push is on its way: a peer's change waits behind it, so the 409 merge keeps the local edit.
  #pushPending() { return this.#syncTimer !== null || this.#busy > 0; }

  // Debounce a save-back after an edit so peers get a `project-event`. No-op for local-only
  // projects and when "Sync changes to server" is off.
  scheduleRemoteSync() {
    const app = this.app;
    if (!app.remoteLink || !getSyncToServer()) return;
    if (this.#reloadingRemote) { this.#syncPending = true; return; }
    // Trailing debounce capped by a max-wait, so CONTINUOUS editing still flushes every ~1.5s.
    const now = Date.now();
    if (!this.#syncFirstAt) this.#syncFirstAt = now;
    const wait = Math.max(0, Math.min(350, 1500 - (now - this.#syncFirstAt)));
    clearTimeout(this.#syncTimer);
    this.#syncTimer = setTimeout(() => {
      this.#syncTimer = null;
      this.#syncFirstAt = 0;
      if (!app.remoteLink || !getSyncToServer()) return this.#pullIfPending();
      if (this.#reloadingRemote) { this.#syncPending = true; return; }
      this.#enqueue(() => this.#pushLayout(false)).then((link) => { if (link) this.#results.markDirty(); });
    }, wait);
  }

  // A server project-event for the project we're editing (wired from stencilApi onChange).
  onServerProjectEvent(msg, conn) {
    const app = this.app;
    if (msg?.type === 'feed-resumed') return void this.#pull.resumed(conn, (m) => this.onServerProjectEvent(m, conn));
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
    const opts = { lastLocalSaveAt: this.#lastRemoteSaveAt, isDrawing: app.isDrawing, connUrl: conn ? conn.url : null };
    if (!shouldReloadFromEvent(msg, app.remoteLink, opts)) {
      // Inside our echo window: asked once more after it, when the link tells our echo from a peer.
      if (shouldReloadFromEvent(msg, app.remoteLink, { ...opts, lastLocalSaveAt: 0 })) {
        const live = this.#liveOn(app.remoteLink);
        setTimeout(() => { if (live()) this.onServerProjectEvent(msg, conn); }, ECHO_WINDOW_MS);
      }
      return;
    }
    if (this.#reloadingRemote || this.#pushPending()) { this.#reloadPending = msg.project; return; }
    this.reloadRemoteActive();
  }

  // The deferred pull, once nothing is in flight; skipped when the link reached the peer's version
  // (the push merged it) and the record names the picture on screen.
  #pullIfPending() {
    const rec = this.#reloadPending;
    if (!rec || this.#reloadingRemote || this.#pushPending()) return;
    this.#reloadPending = null;
    const merged = samePicture(this.#imageSig, rec) && (rec.version ?? 0) <= (this.app.remoteLink?.version ?? 0);
    if (!merged) this.reloadRemoteActive();
  }

  // The server record of the picture just loaded, so a later peer event can tell a layout
  // edit (adopted in place) from a new picture (reloaded).
  noteServerImage(rec) { this.#imageSig = imageSignature(rec); }

  // This editor's own upload is now the server's original ('' = its hash is unknown).
  noteOwnOriginal(hash) { this.#imageSig = withOriginal(this.#imageSig, hash); }

  // Leaving the project (a switch, a new editor): the pending push and pull are dropped — the
  // edit is saved locally — and every round trip still out answers to the project it started on.
  detach() {
    clearTimeout(this.#syncTimer);
    this.#syncTimer = null;
    this.#syncFirstAt = 0;
    this.#syncPending = false;
    this.#reloadPending = null;
    this.#reloadingRemote = false;
    this.#imageSig = '';
    this.#epoch++;
  }

  // The same original (equal originalHash): the layout in place, crop and turn too, history kept.
  // Otherwise the original/source is the base + the stored layout re-applied — never the baked `result`.
  async reloadRemoteActive() {
    const app = this.app;
    const link = app.remoteLink;
    if (!link || this.#reloadingRemote) return;
    const conn = app.connections && app.connections.get(link.address);
    if (!conn) return;
    const live = this.#liveOn(link);
    this.#reloadingRemote = true;
    try {
      const full = await conn.getProject(link.remoteId);
      if (!live()) return;
      if (app.image && samePicture(this.#imageSig, full.project) && applyPeerLayout(app, full.layout, this)) {
        app.remoteLink = { ...app.remoteLink, version: full.project?.version ?? link.version };
      } else if (!(await this.#pull.reloadPicture(conn, link, full, live))) return;
      this.#imageSig = imageSignature(full.project);
      this.#pull.adoptPeerName(full.project?.name);
      this.#toast('pull', 'ok', 'Updated from server', 'ok');
    } catch { if (live()) this.#toast('pull', 'failed', "Couldn't refresh from server — showing the last loaded version", 'info'); }
    finally {
      setTimeout(() => {
        if (!live()) return;
        this.#reloadingRemote = false;
        if (this.#syncPending) { this.#syncPending = false; this.scheduleRemoteSync(); }
        this.#pullIfPending();
      }, 120);
    }
  }

  fetchRemoteOriginal(conn, remoteId, src) { return this.#pull.fetchRemoteOriginal(conn, remoteId, src); }

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
      live: this.#liveOn(this.app.remoteLink),
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
