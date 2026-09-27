// The server writes of a co-edit session: the version-guarded layout push with its 409
// merge-and-retry loop, and the rendered result upload. `hooks` carries the controller's side:
// the toast policy, the echo clock and the filter adoption a merge needs.
import { notify } from '../../utils.js';
import { mergeLines, sanitizeLines, capLayoutPoints } from '../layout.js';
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

// A peer saved first: merge their lines, adopt the server version — re-merged on every pass,
// so repeated bumps can't drop our edit.
const mergePeer = async (app, conn, hooks) => {
  const full = await conn.getProject(app.remoteLink.remoteId);
  app.remoteLink = { ...app.remoteLink, version: full.project?.version ?? app.remoteLink.version };
  const sl = full.layout || {};
  app.lines = capLayoutPoints(mergeLines(sanitizeLines(sl.lines), app.lines));
// A line-only edit must not clobber a peer's filter change (the scalar can't merge).
  if (!app.filterDirty) hooks.adoptServerFilter(sl);
  app.history.push(editorMemento(app));
  app.renderer.redraw();
};

// The link after the push, or null (not linked, failed, or never converged — then reloaded).
export const pushLayout = async (app, hooks) => {
  if (!app.remoteLink || !getSyncToServer()) return null;
  let conn;
  try {
    conn = requireConnection(app.connections, app.remoteLink.address);
  } catch (err) {
    hooks.toast('failed', err.message, 'fail');
    return null;
  }
  const name = projectName(app);
  for (let attempt = 0; attempt < MAX_TRIES; attempt++) {
    const layout = currentLayoutPayload(app);
    hooks.saved();
    try {
      app.remoteLink = await saveRemoteProject(conn, app.remoteLink, { name, layout });
      hooks.saved();
      app.filterDirty = false;
      hooks.toast('ok', attempt === 0 ? 'Saved to server' : 'Merged changes from another editor', 'ok');
      return app.remoteLink;
    } catch (err) {
      if (!err || !err.conflict) {
        hooks.toast('failed', `Server save failed — ${err.message}`, 'fail');
        return null;
      }
      try { await mergePeer(app, conn, hooks); } catch { /* fetch failed; loop retries with current state */ }
    }
  }
  // Reload so the user sees a consistent state (our lines were merged in by an earlier pass).
  notify('Sync conflict — reloaded latest from the server', 'info');
  hooks.reload();
  return null;
};

// The link and what the result paints, as they are right now; the render itself runs later, in the
// image worker when there is one (worker/imageTasks.js resultPngBytes).
export const captureResult = (app) => ({ link: app.remoteLink, job: app.image ? restingJob(app) : null });

// The link that is still open adopts the version the upload bumped to.
export const putResult = async (app, { link, job }, hooks) => {
  if (!link || !job || !getSyncToServer()) return;
  try {
    const conn = requireConnection(app.connections, link.address);
    const bytes = await resultPngBytes(job);
    if (!bytes) return;
    hooks.saved();
    const next = await putRemoteResult(conn, link, { bytes, ext: 'png', w: job.width, h: job.height });
    hooks.saved();
    const cur = app.remoteLink;
    if (cur && cur.remoteId === link.remoteId && cur.address === link.address && next.version > cur.version)
      app.remoteLink = { ...cur, version: next.version };
  } catch (err) { hooks.toast('failed', `Server save failed — ${err.message}`, 'fail'); }
};
