// Ported from js/core/projectTransferController.js: move/copy of a project between local
// storage and a collaboration server, both directions. `c` is the ProjectTransferController —
// these read its storage/tabs/host/remoteSync deps and call back into c.switchToProject.
import { PROJECT_ACTION } from '../worker/messages.js';
import { buildLayoutPayload, normalizeCropRect } from './layout.js';
import { buildExternalLaunchUrl } from './deepLink.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../net/remoteSync.js';

// ── Move / copy a project between local storage and a server ──────
// Create a NEW server project from a local project's content (original bytes + annotated
// layout) under `name`. Shared by move (then links the local) and copy (leaves local as-is).
// Returns { link, proj, meta }. Flushes the active project first so the server gets latest.
export async function createServerFromLocal(c, id, address, name = null) {
  const conn = requireConnection(c.getConnections(), address);
  if (id === c.host.activeProjectId && !c.storage.temporary) c.storage.save();   // flush latest
  const proj = c.storage.store.get(id);
  if (!proj) throw new Error('Project not found');
  const meta = c.storage.store.getMeta(id) || {};
  const payload = proj.payload || {};
  const layout = payload.layout || {};
  // Decode the stored original (a data URL) to raw bytes for the codec-free server.
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
  // Push the annotated layout (lines + filter) so the server holds the full project.
  // The layout save bumps the server version again, so adopt the refreshed link it
  // returns — otherwise `link.version` stays at the create-time value and the next
  // version-guarded field push (colour / rename / expiry) 409s against the server.
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

// Local → server: create the project on `address`, then LINK the local copy to it (keeping
// the editor open + the row in place). Returns the new remote id.
export async function moveProjectToServer(c, id, address) {
  const { link, proj, meta } = await createServerFromLocal(c, id, address);
  const linkedMeta = { ...meta, id, address: link.address, remoteId: link.remoteId, remoteVersion: link.version };
  c.storage.store.upsert(linkedMeta, proj.payload || {});
  if (id === c.host.activeProjectId) {
    c.host.remoteLink = { address: link.address, remoteId: link.remoteId, version: link.version };
    c.host.updateProjectTitle();   // reflect the golden remote outline now
  }
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  return link.remoteId;
}

// Local → server COPY: create a new server project from the local one (default name
// "<name>-copy") and LEAVE the local project untouched. Returns the new remote id.
export async function copyProjectToServer(c, id, address, { name } = {}) {
  const base = c.storage.store.getMeta(id)?.name || 'Untitled';
  const copyName = (name && name.trim()) || `${base}-copy`;
  const { link } = await createServerFromLocal(c, id, address, copyName);
  c.tabs.projectsChanged({ action: PROJECT_ACTION.UPDATED });   // refresh the remote rows
  return link.remoteId;
}

// Server → local: fetch the server project's image + layout, save it as a new
// local project, then delete it from the server. `meta` is a remote-project meta
// ({ id, serverUrl, name, source }). Returns the new local project id.
export async function moveProjectToLocal(c, meta) {
  const host = c.host;
  // If the moved server project is the open session (or its local cache), follow it to the
  // new local id so the editor stays open + focused instead of pointing at a deleted server id.
  const openCacheId = (host.remoteLink && host.remoteLink.remoteId === meta.id
    && host.remoteLink.address === meta.serverUrl) ? host.activeProjectId : null;
  const newId = await importServerProjectToLocal(c, meta, { removeFromServer: true });
  if (openCacheId != null) {
    if (openCacheId !== newId) c.storage.store.remove(openCacheId);   // drop the now-stale cache
    c.switchToProject(newId);
  }
  return newId;
}

// Make a detached LOCAL copy of a server project, leaving the server copy in place. Default
// name "<name>-copy" (override via `name`). Returns the new local project id; caller opens it.
export async function copyServerProjectToLocal(c, meta, { name } = {}) {
  return importServerProjectToLocal(c, meta, { removeFromServer: false, copy: true, name });
}

// Copy a server project into an INCOGNITO session (no local record, no server link). Current
// tab: replace the editor with the image + annotations as incognito. New tab: hand off the
// image via the external-launch URL (image only — the launch payload carries no annotations).
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
  if (!c.storage.incognito) c.storage.save();   // flush any current project first
  c.host.newEditor();
  c.storage.incognito = true;
  c.host.updateIncognitoUI();
  // adoptLayout applies the lines/filter/crop/page/formulas without linking (no remoteId).
  c.host.loadImageFromFile(file, { source: src, resource: full.project?.resource || '', layout: full.layout, adoptLayout: true });
}

// Read a Blob into a data URL (used by the new-tab incognito hand-off).
export function blobToDataUrl(c, blob) {
  return new Promise((res, rej) => {
    const r = new FileReader();
    r.onload = () => res(r.result);
    r.onerror = () => rej(new Error('could not read image bytes'));
    r.readAsDataURL(blob);
  });
}

// Shared body of move/copy server→local: fetch image + layout, persist a fresh detached
// local project (crop/rotation included), optionally delete the server copy. `copy` defaults
// the name to "<base>-copy"; an explicit `name` overrides.
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
      cropRect: normalizeCropRect(sl.cropRect),   // server rects are canonical {w,h}; store internal shape
      rotationQuarters: sl.rotationQuarters || 0,
      // Carry page format + formulas so the detached local copy keeps them.
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
  // Remove from the server only for a move (the live feed re-renders its golden row out).
  if (removeFromServer) await conn.deleteProject(meta.id);
  c.tabs.projectsChanged({ id: newId, action: PROJECT_ACTION.UPDATED });
  return newId;
}
