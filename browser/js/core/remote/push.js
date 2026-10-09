// The server writes of a co-edit session: the version-guarded layout push with its 409
// merge-and-retry loop, and the rendered result upload. `hooks` carries the controller's side:
// the toast policy, the echo clock and the filter adoption a merge needs.
import { notify } from '../../utils.js';
import { mergeLines, sanitizeLines, capLayoutPoints, lineDedupeKey } from '../layout.js';
import { editorMemento } from '../historyStack.js';
import { getSyncToServer } from '../../net/connectionStore.js';
import { requireConnection, saveRemoteProject, putRemoteResult } from '../../net/remoteSync.js';
import { restingJob } from '../draw/restingPaint.js';
import { resultPngBytes } from '../../worker/imageTasks.js';
import { currentLayoutPayload } from '../project/meta/projectMeta.js';

const MAX_TRIES = 6;

const projectName = (app) => (app.activeProjectId != null
  ? (app.storage.store.getMeta(app.activeProjectId)?.name || app.imageBaseName || 'Untitled')
  : (app.imageBaseName || 'Untitled'));

export const sameLink = (a, b) => !!a && !!b && a.remoteId === b.remoteId && a.address === b.address;

// A peer saved first: merge their lines and adopt the server version. Each pass merges against
// what the last pass saw: a line of that peer set is theirs, so a peer's move never resurrects it.
const mergePeer = async (app, conn, hooks, seen, stale) => {
  const full = await conn.getProject(app.remoteLink.remoteId);
  if (stale()) return;
  app.remoteLink = { ...app.remoteLink, version: full.project?.version ?? app.remoteLink.version };
  const sl = full.layout || {};
  const peer = sanitizeLines(sl.lines);
  const local = app.lines.filter((l) => !seen.peerKeys.has(lineDedupeKey(l)));
  seen.peerKeys = new Set(peer.map(lineDedupeKey));
  app.lines = capLayoutPoints(mergeLines(peer, local));
  seen.merged = true;
// A line-only edit must not clobber a peer's filter change (the scalar can't merge).
  if (!app.filterDirty) hooks.adoptServerFilter(sl);
  app.renderer.redraw();
};

// The link after the push, or null (not linked, failed, abandoned by a project switch, or never
// converged — then reloaded).
export const pushLayout = async (app, hooks) => {
  if (!app.remoteLink || !getSyncToServer()) return null;
  let conn;
  try {
    conn = requireConnection(app.connections, app.remoteLink.address);
  } catch (err) {
    hooks.toast('failed', err.message, 'fail');
    return null;
  }
  const started = app.remoteLink;
  // The editor left this project while a round trip was out: what came back lands nowhere.
  const stale = () => !hooks.live() || !sameLink(app.remoteLink, started);
  const name = projectName(app);
  const seen = { peerKeys: new Set(), merged: false };
  // One undo step for the whole save, however many passes it merged.
  const settle = (out) => {
    if (seen.merged) app.history.push(editorMemento(app));
    return out;
  };
  for (let attempt = 0; attempt < MAX_TRIES; attempt++) {
    const layout = currentLayoutPayload(app);
    hooks.saved();
    try {
      const next = await saveRemoteProject(conn, app.remoteLink, { name, layout });
      if (stale()) return null;
      app.remoteLink = next;
      hooks.saved();
      app.filterDirty = false;
      hooks.toast('ok', attempt === 0 ? 'Saved to server' : 'Merged changes from another editor', 'ok');
      return settle(app.remoteLink);
    } catch (err) {
      if (stale()) return null;
      if (!err || !err.conflict) {
        hooks.toast('failed', `Server save failed — ${err.message}`, 'fail');
        return settle(null);
      }
      try { await mergePeer(app, conn, hooks, seen, stale); } catch { /* fetch failed; loop retries with current state */ }
      if (stale()) return null;
    }
  }
  settle();
  // Reload so the user sees a consistent state (our lines were merged in by an earlier pass).
  notify('Sync conflict — reloaded latest from the server', 'info');
  hooks.reload();
  return null;
};

// The link and what the result paints, as they are right now; the render itself runs later, in the
// image worker when there is one (worker/imageTasks.js resultPngBytes).
export const captureResult = (app) => ({ link: app.remoteLink, job: app.image ? restingJob(app) : null });

// The write is measured against the link as it stands when it starts, so a push queued before it
// does not hide this write's own bump; the still-open link adopts only that bump.
export const putResult = async (app, { link, job }, hooks) => {
  if (!link || !job || !getSyncToServer()) return;
  try {
    const conn = requireConnection(app.connections, link.address);
    const bytes = await resultPngBytes(job);
    if (!bytes) return;
    hooks.saved();
    const base = sameLink(app.remoteLink, link) ? app.remoteLink : link;
    const next = await putRemoteResult(conn, base, { bytes, ext: 'png', w: job.width, h: job.height });
    hooks.saved();
    const cur = app.remoteLink;
    if (sameLink(cur, link) && next.version > cur.version) app.remoteLink = { ...cur, version: next.version };
  } catch (err) { hooks.toast('failed', `Server save failed — ${err.message}`, 'fail'); }
};
