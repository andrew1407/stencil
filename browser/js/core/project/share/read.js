// What a project row's Copy › entries read, each only once it is picked: a layout never loads an
// image, a server row's layout is its record alone, an image is one file. `c` is the transfer
// controller (copy/source.js's host); a target is { id } (a stored row) or { remote } (a listing row).
import { readCopySource, readRemoteSource } from '../copy/source.js';
import { buildLayoutPayload } from '../../layout.js';
import { serializeProjectFile } from '../file.js';
import { requireConnection } from '../../../net/remoteSync.js';
import { isDataUrl, blobOfDataUrl } from '../store/thumbBlobs.js';

const connOf = (c, remote) => requireConnection(c.getConnections(), remote.serverUrl);

// The layout as a download or a paste carries it.
export const readShareLayout = async (c, { id = null, remote = null }) => {
  if (remote) return buildLayoutPayload((await connOf(c, remote).getProject(remote.id)).layout || {});
  const proj = c.storage.store.get(id);
  if (!proj) throw new Error('Project not found');
  return buildLayoutPayload(proj.payload?.layout || {});
};

// The whole `.stencil` text: its record, original and layout.
export const readShareProjectText = async (c, { id = null, remote = null }) => {
  const src = remote ? await readRemoteSource(c, remote) : await readCopySource(c, id);
  const { meta, payload } = src;
  const image = { dataUrl: payload.image, ext: payload.layout?.imageExt || 'png', w: meta.imageW, h: meta.imageH };
  return serializeProjectFile({ ...meta, image, layout: payload.layout });
};

// What a picture is drawn from: the untouched original (the server's file or its source URL, else the
// stored data URL) and the layout as stored, its crop, turn, filter and lines.
export const readShareRenderSource = async (c, { id = null, remote = null }) => {
  if (remote) {
    const conn = connOf(c, remote);
    const [full, blob] = await Promise.all([conn.getProject(remote.id),
      c.remoteSync.fetchRemoteOriginal(conn, remote.id, remote.source || '')]);
    if (!blob) throw new Error('no image bytes on the server');
    return { blob, layout: full.layout || {} };
  }
  const proj = c.storage.store.get(id);
  if (!proj) throw new Error('Project not found');
  const dataUrl = await c.storage.store.resolveImage(id, proj.payload?.image, false);
  if (!isDataUrl(dataUrl)) throw new Error('That project has no image');
  return { blob: blobOfDataUrl(dataUrl), layout: proj.payload?.layout || {} };
};
