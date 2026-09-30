// A copy made on the source's SERVER: a new project from the original's bytes, its layout
// saved back version-guarded, and its chat file for a whole-project copy. Nothing is linked.
import { PROJECT_ACTION } from '../../../worker/messages.js';
import { buildLayoutPayload } from '../../layout.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../../../net/remoteSync.js';
import { guardedFetch } from '../../../net/fetchGuard.js';
import { blobOfDataUrl } from '../store/thumbBlobs.js';
import { copyPayload, copyScopes } from './scope.js';

const imageBlob = async (image) => (/^data:/i.test(image) ? blobOfDataUrl(image) : (await guardedFetch(image)).blob());

// The names the server already holds, so "-copy(N)" numbers past them too.
export const serverNames = async (c, address) => {
  try {
    const conn = requireConnection(c.getConnections(), address);
    return (await conn.listProjects()).map((p) => ({ id: `remote:${p.id}`, name: p.name }));
  } catch { return []; }
};

export const createServerCopy = async (c, src, what, name) => {
  const conn = requireConnection(c.getConnections(), src.remote.address);
  const { meta, payload } = copyPayload(src, what, { id: null, name });
  const blob = src.blob ?? await imageBlob(payload.image);
  const scopes = copyScopes(what);
  const layout = src.payload.layout || {};
  let link = await createRemoteProject(conn, {
    name, source: meta.source || '', resource: meta.resource || '', color: meta.color,
    description: meta.description, keywords: meta.keywords, blankColor: meta.blank ? meta.blankColor : '',
    expiresAt: scopes.meta ? meta.expiresAt : 0,
    bytes: new Uint8Array(await blob.arrayBuffer()),
    ext: (blob.type && blob.type.split('/')[1]) || layout.imageExt || 'png',
    w: layout.imageWidth || 0, h: layout.imageHeight || 0,
  });
  if (scopes.layout) link = await saveRemoteProject(conn, link, { name, layout: buildLayoutPayload(payload.layout) });
  if (scopes.chat) {
    const from = src.srcId != null ? { id: src.srcId } : { conn, remoteId: src.remote.remoteId };
    await c.host.chatPersistence?.projectCopied?.(from, { conn, remoteId: link.remoteId });
  }
  c.tabs.projectsChanged({ action: PROJECT_ACTION.UPDATED });
  return link;
};
