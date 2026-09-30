// A copy saved as a new LOCAL project: one upsert of the scoped row + payload, the source's
// thumbnail when the copy shows what it shows, and its chat for a whole-project copy.
import { PROJECT_ACTION } from '../../../worker/messages.js';
import { guardedFetch } from '../../../net/fetchGuard.js';
import { blobToDataUrl } from '../serverTransfer.js';
import { copyPayload, copyScopes } from './scope.js';

// The backend stores a thumbnail only as a data URL; a row's is read back from its object URL.
const thumbDataUrl = async (c, thumb) => {
  if (typeof thumb !== 'string' || !thumb) return null;
  if (/^data:/i.test(thumb)) return thumb;
  try { return await blobToDataUrl(c, await (await guardedFetch(thumb)).blob()); } catch { return null; }
};

export const createLocalCopy = async (c, src, what, name) => {
  const store = c.storage.store;
  const id = store.createId();
  const { meta, payload } = copyPayload(src, what, { id, name });
  // An image-only copy renders its own on first open: the source's shows its lines.
  if (copyScopes(what).layout) meta.thumbnail = await thumbDataUrl(c, src.meta?.thumbnail);
  store.upsert(meta, payload);
  if (copyScopes(what).chat && src.srcId != null) {
    await c.host.chatPersistence?.projectCopied?.({ id: src.srcId }, { id });
  }
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  return id;
};
