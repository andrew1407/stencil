// Move/copy of a project between local storage and a collaboration server, both
// directions. `c` is the ProjectTransferController.
import { PROJECT_ACTION } from '../worker/messages.js';
import { buildLayoutPayload, normalizeCropRect } from './layout.js';
import { buildExternalLaunchUrl } from './deepLink.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../net/remoteSync.js';

// A NEW server project from a local project's content under `name`; shared by move
// (then links the local) and copy. Returns { link, proj, meta }.
export async function createServerFromLocal(c, id, address, name = null) {
  const conn = requireConnection(c.getConnections(), address);
  if (id === c.host.activeProjectId && !c.storage.temporary) c.storage.save();
  const proj = c.storage.store.get(id);
  if (!proj) throw new Error('Project not found');
  const meta = c.storage.store.getMeta(id) || {};
  const payload = proj.payload || {};
  const layout = payload.layout || {};
  // The stored original (a data URL) → raw bytes for the codec-free server.
  let bytes = null;
  let ext = meta.imageExt || layout.imageExt || 'png';
  const w = layout.imageWidth || meta.imageW || 0;
  const h = layout.imageHeight || meta.imageH || 0;
  if (payload.image) {
    const blob = await (await fetch(payload.image)).blob();
    bytes = new Uint8Array(await blob.arrayBuffer());
    if (blob.type && blob.type.includes('/')) ext = blob.type.split('/')[1];
  }
  const projName = (name && name.trim()) || meta.name || layout.imageBaseName || 'Untitled';
  const link = await createRemoteProject(conn, {
    name: projName,
    source: meta.source || layout.imageSource || '',
    resource: meta.resource || layout.imageResource || '',
    color: meta.color || '',
    bytes, ext, w, h,
  });
  // The layout save bumps the server version again: adopt the refreshed link, or the next
  // version-guarded field push 409s.
  const savedLink = await saveRemoteProject(conn, link, {
    name: projName,
    layout: buildLayoutPayload({
      imageWidth: w, imageHeight: h,
      lines: layout.lines || [],
      imageFilter: layout.imageFilter,
      filterColor: layout.filterColor,
      cropRect: layout.cropRect,
      rotationQuarters: layout.rotationQuarters,
      pageSize: layout.pageSize,
      customPageWidth: layout.customPageWidth,
      customPageHeight: layout.customPageHeight,
      allowFormulas: layout.allowFormulas,
      formulaX: layout.formulaX,
      formulaY: layout.formulaY,
    }),
  });
  return { link: savedLink, proj, meta };
}

// Local → server: create on `address`, then LINK the local copy (editor + row stay). Returns the remote id.
export async function moveProjectToServer(c, id, address) {
  const { link, proj, meta } = await createServerFromLocal(c, id, address);
  const linkedMeta = { ...meta, id, address: link.address, remoteId: link.remoteId, remoteVersion: link.version };
  c.storage.store.upsert(linkedMeta, proj.payload || {});
  if (id === c.host.activeProjectId) {
    c.host.remoteLink = { address: link.address, remoteId: link.remoteId, version: link.version };
    c.host.updateProjectTitle();
  }
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  return link.remoteId;
}

// Local → server COPY (default name "<name>-copy"); the local project stays untouched.
export async function copyProjectToServer(c, id, address, { name } = {}) {
  const base = c.storage.store.getMeta(id)?.name || 'Untitled';
  const copyName = (name && name.trim()) || `${base}-copy`;
  const { link } = await createServerFromLocal(c, id, address, copyName);
  c.tabs.projectsChanged({ action: PROJECT_ACTION.UPDATED });
  return link.remoteId;
}

// Server → local: save as a new local project, then delete from the server. `meta` is a
// remote-project meta ({ id, serverUrl, name, source }). Returns the new local id.
export async function moveProjectToLocal(c, meta) {
  const host = c.host;
  // The moved project is the open session: follow it to the new local id.
  const openCacheId = (host.remoteLink && host.remoteLink.remoteId === meta.id
    && host.remoteLink.address === meta.serverUrl) ? host.activeProjectId : null;
  const newId = await importServerProjectToLocal(c, meta, { removeFromServer: true });
  if (openCacheId != null) {
    if (openCacheId !== newId) c.storage.store.remove(openCacheId);
    c.switchToProject(newId);
  }
  return newId;
}

// A detached LOCAL copy (default name "<name>-copy"); the server copy stays. Returns the new local id.
export async function copyServerProjectToLocal(c, meta, { name } = {}) {
  return importServerProjectToLocal(c, meta, { removeFromServer: false, copy: true, name });
}

// Copy a server project into an INCOGNITO session. New tab: the external-launch URL carries
// the image only (no annotations).
export async function copyServerProjectToIncognito(c, meta, { newTab = false } = {}) {
  const conn = requireConnection(c.getConnections(), meta.serverUrl);
  const full = await conn.getProject(meta.id);
  const src = full.project?.source || meta.source || '';
  const blob = await c.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
  if (!blob) throw new Error('no image bytes on the server');
  const ext = (blob.type && blob.type.split('/')[1]) || 'png';
  const name = full.project?.name || meta.name || 'Untitled';
  if (newTab) {
    const dataUrl = await blobToDataUrl(c, blob);
    const url = buildExternalLaunchUrl(location.origin + location.pathname, { dataUrl, name, incognito: true });
    window.open(url, '_blank');
    return;
  }
  const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
  if (!c.storage.incognito) c.storage.save();
  c.host.newEditor();
  c.storage.incognito = true;
  c.host.updateIncognitoUI();
  c.host.loadImageFromFile(file, { source: src, resource: full.project?.resource || '', layout: full.layout, adoptLayout: true });
}

export function blobToDataUrl(c, blob) {
  return new Promise((res, rej) => {
    const r = new FileReader();
    r.onload = () => res(r.result);
    r.onerror = () => rej(new Error('could not read image bytes'));
    r.readAsDataURL(blob);
  });
}

// Shared body of move/copy server→local; `copy` defaults the name to "<base>-copy".
export async function importServerProjectToLocal(c, meta, { removeFromServer = false, copy = false, name = null } = {}) {
  const conn = requireConnection(c.getConnections(), meta.serverUrl);
  const full = await conn.getProject(meta.id);
  const src = full.project?.source || meta.source || '';
  const blob = await c.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
  const dataUrl = blob ? await blobToDataUrl(c, blob) : null;
  const sl = full.layout || {};
  const newId = c.storage.store.createId();
  const base = full.project?.name || meta.name || 'Untitled';
  const projName = (name && name.trim()) || (copy ? `${base}-copy` : base);
  const localMeta = {
    id: newId,
    name: projName,
    color: full.project?.color || '',
    thumbnail: dataUrl,
    createdAt: Date.now(),
    hasImage: !!dataUrl,
    imageW: sl.imageWidth || 0,
    imageH: sl.imageHeight || 0,
    source: src || null,
    resource: full.project?.resource || null,
    address: null,
    remoteId: null,
    remoteVersion: 0,
  };
  c.storage.store.upsert(localMeta, {
    image: dataUrl,
    layout: {
      imageWidth: sl.imageWidth || 0,
      imageHeight: sl.imageHeight || 0,
      lines: Array.isArray(sl.lines) ? sl.lines : [],
      imageFilter: sl.imageFilter || 'none',
      filterColor: sl.filterColor || '#7c3aed',
      cropRect: normalizeCropRect(sl.cropRect),
      rotationQuarters: sl.rotationQuarters || 0,
      pageSize: sl.pageSize || 'A3',
      customPageWidth: sl.customPageWidth || 21,
      customPageHeight: sl.customPageHeight || 29.7,
      allowFormulas: !!sl.allowFormulas,
      formulaX: sl.formulaX || '',
      formulaY: sl.formulaY || '',
      imageBaseName: projName,
      imageExt: (blob && blob.type && blob.type.includes('/')) ? blob.type.split('/')[1] : 'png',
      imageSource: src || null,
      imageResource: full.project?.resource || null,
    },
  });
  // Only a move removes it (the live feed re-renders its golden row out).
  if (removeFromServer) await conn.deleteProject(meta.id);
  c.tabs.projectsChanged({ id: newId, action: PROJECT_ACTION.UPDATED });
  return newId;
}
