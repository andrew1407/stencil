// What a copy is taken from, as one shape: a registry row + payload with its image as a data URL,
// plus the server it is linked to. The live editor (unsaved edits, incognito, temporary) is read
// through the host; a stored row from the store; a server-only row from its server.
import { buildLayoutState, buildProjectMeta } from '../meta/projectMeta.js';
import { resolveSource } from '../store/projectSources.js';
import { requireConnection } from '../../../net/remoteSync.js';
import { dataUrlOfBlob } from '../store/imageBlobs.js';

// The live editor as a registry row + payload: its saved row's meta under what is on screen now.
export const liveProjectSnapshot = (app) => {
  const saved = !app.storage.incognito && !app.storage.temporary && app.activeProjectId != null;
  const prev = (saved && app.storage.store.getMeta(app.activeProjectId)) || {};
  const layout = buildLayoutState(app);
  const thumb = prev.thumbnail ? { thumbnail: prev.thumbnail } : {};
  const meta = buildProjectMeta(app, { prev, id: app.activeProjectId, layout, ...thumb });
  return { srcId: saved ? app.activeProjectId : null, meta, payload: { image: app.imageDataUrl || null, layout } };
};

const remoteOf = (meta) => (meta?.address && meta?.remoteId ? { address: meta.address, remoteId: meta.remoteId } : null);

// `id` null, or the active project's own id, is the live editor.
export const readCopySource = async (c, id = null) => {
  if (id == null || id === c.host.activeProjectId) {
    await c.storage.imageReady;
    const snap = c.host.liveProject();
    if (!snap?.payload?.image) throw new Error('There is no image to copy');
    return { ...snap, remote: remoteOf(snap.meta) };
  }
  const proj = c.storage.store.get(id);
  if (!proj) throw new Error('Project not found');
  const layout = proj.payload?.layout || {};
  const image = await c.storage.store.resolveImage(id, proj.payload?.image, false);
  if (!image) throw new Error('That project has no image to copy');
  const meta = { ...proj.meta, source: resolveSource(proj.meta.source, layout.imageSource) };
  return { srcId: id, meta, payload: { image, layout }, remote: remoteOf(meta) };
};

// A server listing row ({ id, serverUrl, … }): its record, original and layout, fetched; `blob`
// keeps the original's bytes so a copy onto the server never decodes the data URL back.
export const readRemoteSource = async (c, row) => {
  const conn = requireConnection(c.getConnections(), row.serverUrl);
  const full = await conn.getProject(row.id);
  const p = full.project || {};
  const src = p.source || row.source || '';
  const blob = await c.remoteSync.fetchRemoteOriginal(conn, row.id, src);
  if (!blob) throw new Error('no image bytes on the server');
  const layout = {
    ...(full.layout || {}),
    imageBaseName: p.name || row.name || null,
    imageExt: (blob.type && blob.type.split('/')[1]) || 'png',
    imageSource: src || null,
    imageResource: p.resource || null,
  };
  const meta = {
    name: p.name || row.name || 'Untitled', color: p.color || '', description: p.description || '',
    keywords: Array.isArray(p.keywords) ? p.keywords : [], blank: !!p.blankColor, blankColor: p.blankColor || '',
    expiresAt: p.expiresAt || 0, source: src || null, resource: p.resource || null,
    imageW: layout.imageWidth || 0, imageH: layout.imageHeight || 0,
  };
  const image = await dataUrlOfBlob(blob);
  return { srcId: null, meta, payload: { image, layout }, blob, remote: { address: row.serverUrl, remoteId: row.id } };
};
