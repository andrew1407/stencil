// A project's image under its own key beside its payload, so a save re-serialises the small layout
// and rewrites the image only when it changed. A payload an older build wrote keeps its image inline
// and still loads; its next save moves the image out. Through projectsBackend.js the key is a Blob,
// read back as an object URL until `resolveImage` turns it into its data URL.
import { storageKeys } from './projectRegistryIo.js';

export const IMAGE_PREFIX = 'stencil_image_';
const keyOf = (id) => IMAGE_PREFIX + id;

export const readImage = (storage, id) => {
  try { return storage.getItem(keyOf(id)); } catch { return null; }
};

// A string lands when it differs from the stored one — the same string object compares at once —
// and anything else clears the key. QuotaExceededError propagates, like every other store write.
export const writeImage = (storage, id, image) => {
  const cur = readImage(storage, id);
  if (typeof image === 'string') { if (cur !== image) storage.setItem(keyOf(id), image); }
  else if (cur != null) storage.removeItem(keyOf(id));
};

// The payload as the payload key stores it: everything but the image.
export const withoutImage = (payload) => {
  const { image: _image, ...rest } = payload || {};
  return rest;
};

// The payload as callers read it: an inline image (an older build's) as it is, else its key's.
export const withImage = (storage, id, payload) =>
  (Object.hasOwn(payload, 'image') ? payload : { ...payload, image: readImage(storage, id) });

// The image as a data URL: an object URL a backend handed out is read back (and, with `keep`, held
// while its project is open); any other value already is one.
export const resolveImage = (storage, id, image, keep = true) =>
  (typeof image === 'string' && image.startsWith('blob:') && storage?.materialize
    ? storage.materialize(keyOf(id), keep).catch(() => image)
    : Promise.resolve(image ?? null));

export const removeImage = (storage, id) => {
  try { storage.removeItem(keyOf(id)); } catch { /* the registry row is the source of truth */ }
};

export const clearImages = (storage) => {
  for (const k of storageKeys(storage)) {
    if (!k.startsWith(IMAGE_PREFIX)) continue;
    try { storage.removeItem(k); } catch { /* keep wiping the rest */ }
  }
};
