// Putting a new image in front of the user: opening one in this editor (optionally creating
// it on a server), resetting to a blank editor, and replacing the active project's image.
import { requireConnection } from '../net/remoteSync.js';

// An explicit `crop` wins; else `noCrop` imports the whole frame; provenance rides along.
// Returns the mutated target.
export const applyOpenOpts = (target, opts) => {
  if (opts.crop) target.crop = opts.crop;
  else if (opts.noCrop) target.noCrop = true;
  if (opts.source) target.source = opts.source;
  if (opts.resource) target.resource = opts.resource;
  if (opts.landing) target.landing = true;
  if (opts.from) target.from = opts.from;
  return target;
};

// `keepChat` forwards to newTemporary (the assistant's incognito adoption keeps its turn).
export const newEditor = (app, { keepChat = false } = {}) => {
  app.remoteLink = null;
  app.pendingRemoteAddress = null;
  app.blankColor = '';
  app.fromFile = false;
  app.stencilSync?.unlink();
  app.storage.newTemporary({ keepChat });
// Dropping the image can move the viewport's top (the toolbar reflows) — re-measure.
  app.zoomPan.syncViewportHeight();
  app.tabs.reportActive(null);
  app.reportIncognitoSession();
};

// `address` creates+links the project there, but incognito wins over a server target
// (publish explicitly via publishIncognitoToServer). `opts.crop` overrides the auto-crop.
export const openImageHere = (app, file, incognito = false, address = null, opts = {}) => {
  if (!file) return;
  const toServer = !!address && !incognito;
  if (toServer) requireConnection(app.connections, address);
  if (!app.storage.incognito) app.storage.save();
  app.newEditor();
  if (incognito) { app.storage.incognito = true; app.updateIncognitoUI(); }
  app.loadImageFromFile(file, applyOpenOpts(toServer ? { address } : {}, opts));
};

// Same id / server link. `crop` is a rect in the NEW image's pixels; the stale pin drops
// in loadImageFromFile.
export const replaceProjectImage = (app, file, { rename = false, keepAnnotations = true, crop = null } = {}) => {
  if (!file) return;
  app.loadImageFromFile(file, { replaceInPlace: true, rename, keepAnnotations, ...(crop ? { crop } : {}) });
};

// The server forbids image-less projects, so the upcoming blank()/open creates it WITH
// real bytes (imageSettle's createRemoteForSession).
export const createRemoteBlank = async (app, address) => {
  const conn = requireConnection(app.connections, address);
  app.pendingRemoteAddress = conn.url;
  return { address: conn.url };
};
