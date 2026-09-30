// "Make a copy": the one path every entry point takes — the projects menu, the canvas menu, the
// toolbar, window.stencil and the copyProject op (desktop twin: app/project/copy/ProjectCopy.cpp).
// Reads the source, names it "<name>-copy(N)" (copySuffixName), saves it, then opens it.
import { copySuffixName } from '../meta/projectNaming.js';
import { settleCopyOptions } from './options.js';
import { readCopySource, readRemoteSource } from './source.js';
import { copyPayload } from './scope.js';
import { createLocalCopy } from './local.js';
import { createServerCopy, serverNames } from './server.js';
import { openSavedCopy, openIncognitoCopy } from './open.js';

// `id` names a stored row (null: the live editor), `remote` a server-only listing row instead.
// Resolves to the new local id, the server copy's remote id, or null for an unsaved incognito copy.
export const copyProject = async (c, { id = null, remote = null, what, open = 'none', incognito = false,
  local = false, win = null } = {}) => {
  const src = remote ? await readRemoteSource(c, remote) : await readCopySource(c, id);
  const opts = settleCopyOptions({ what, open, incognito, local }, { serverSource: !!src.remote });
  const taken = [...c.storage.store.list(), ...(opts.onServer ? await serverNames(c, src.remote.address) : [])];
  const name = copySuffixName(taken, src.meta?.name);
  if (opts.incognito) {
    openIncognitoCopy(c, opts.open, copyPayload(src, opts.what, { id: null, name }), name, win);
    return null;
  }
  if (opts.onServer) {
    const link = await createServerCopy(c, src, opts.what, name);
    await openSavedCopy(c, opts, link, name, win);
    return link.remoteId;
  }
  const newId = await createLocalCopy(c, src, opts.what, name);
  await openSavedCopy(c, opts, newId, name, win);
  return newId;
};
