// Where a finished copy goes: nowhere, this tab, or a new one — a local row by its id, a server
// copy by its link (a new tab gets `{url, id, version}`, never a token), an unsaved incognito copy
// by its bytes. `win`, opened inside the user's gesture, beats a strict popup blocker.
import { LAUNCH_DATA_URL_MAX } from '../../launch/deepLink.js';
import { blobOfDataUrl } from '../store/thumbBlobs.js';
import { openIncognitoHere } from '../serverTransfer.js';

const OPEN_SAVED = Object.freeze({
  local: {
    here: (c, id) => c.switchToProject(id),
    newtab: (c, id, win) => c.openProjectInNewTab(id, win),
  },
  server: {
    here: (c, link, _win, name) => c.openRemoteProject({ id: link.remoteId, serverUrl: link.address, name }),
    newtab: (c, link, win) => c.openRemoteProjectInNewTab({ id: link.remoteId, serverUrl: link.address, version: link.version }, win),
  },
});

export const openSavedCopy = async (c, { open, onServer }, target, name, win = null) => {
  if (open === 'none') { win?.close(); return; }
  await OPEN_SAVED[onServer ? 'server' : 'local'][open](c, target, win, name);
};

const fileOf = (dataUrl, name, ext) => {
  const blob = blobOfDataUrl(dataUrl);
  return new File([blob], `${name}.${(blob.type && blob.type.split('/')[1]) || ext || 'png'}`, { type: blob.type || 'image/png' });
};

const OPEN_INCOGNITO = Object.freeze({
  here: (c, { payload, meta }, name) => openIncognitoHere(c, fileOf(payload.image, name, payload.layout.imageExt), {
    source: meta.source || '', resource: meta.resource || '', layout: payload.layout,
    ...(meta.blank && meta.blankColor ? { blankColor: meta.blankColor } : {}),
  }),
  newtab: (c, { payload, meta }, name, win) => {
    if (payload.image.length > LAUNCH_DATA_URL_MAX) throw new Error('the image is too large to open in a new tab');
    const launch = { dataUrl: payload.image, name: `${name}.${payload.layout.imageExt || 'png'}`, layout: payload.layout, incognito: true };
    if (meta.source && !/^data:/i.test(meta.source)) launch.source = meta.source;
    if (meta.resource) launch.resource = meta.resource;
    c.openLaunchInNewTab(launch, win);
  },
});

// `copy` is the scoped { meta, payload } (copyPayload); nothing is written anywhere.
export const openIncognitoCopy = (c, open, copy, name, win = null) => OPEN_INCOGNITO[open](c, copy, name, win);
