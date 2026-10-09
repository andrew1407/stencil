// The pull half of a co-edit session: a peer's new original fetched and loaded over the stored
// layout, the peer's rename adopted through the store. The in-place layout adoption is
// peerLayout.js; the version-guarded writes are push.js.
import { guardedFetch } from '../../net/fetchGuard.js';
import { readBlobCapped } from '../../net/cappedBody.js';
import { keptSource } from '../project/store/projectSources.js';
import { loadImageFromFile } from '../image/loadFlow.js';

export class RemotePull {
  constructor(app) { this.app = app; }

  // `live` is asked again after the download: the editor may have left the project meanwhile.
  async reloadPicture(conn, link, full, live) {
    const app = this.app;
    const src = full.project?.source || '';
    const blob = await this.fetchRemoteOriginal(conn, link.remoteId, src);
    if (!blob || !live()) return false;
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
  adoptPeerName(peerName) {
    const app = this.app;
    if (!peerName || app.activeProjectId == null) return;
    if (peerName === app.storage.store.getMeta(app.activeProjectId)?.name) return;
    app.storage.store.rename(app.activeProjectId, peerName);
    app.imageBaseName = peerName;
    app.updateProjectTitle();
  }

  // The feed was down, so a peer's save then sent no event: the linked record is read once and
  // judged as one, under every rule a live event meets.
  async resumed(conn, judge) {
    const link = this.app.remoteLink;
    if (!link || !conn || conn.url !== link.address) return;
    try {
      const full = await conn.getProject(link.remoteId);
      judge({ type: 'project-event', event: 'updated', project: full.project });
    } catch { /* the next event or edit catches up */ }
  }

  // The server's original bytes, else its http(s) source URL (CORS). Blob or null.
  async fetchRemoteOriginal(conn, remoteId, src) {
    let blob = null;
    try { blob = await conn.fetchFile(remoteId, 'original'); } catch {}
    if (!blob && /^https?:/i.test(src || '')) {
      const resp = await guardedFetch(src, { mode: 'cors' });
      if (resp.ok) blob = await readBlobCapped(resp);
    }
    return blob;
  }
}
