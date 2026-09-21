// Incognito: adopting the current turn into an unsaved editor, and the two ways out of it —
// publishing to a server, or keeping it as a local project. Both are explicit user acts.
import { notify } from '../../utils.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../../net/remoteSync.js';

// Incognito can be toggled only while the editor is blank, since adding content auto-saves.
export const canToggleIncognito = (app) =>
  app.storage.temporary && app.activeProjectId == null &&
  !app.image && app.lines.length === 0;

// For the projects modal's "Incognito tabs" filter. Best-effort.
export const reportIncognitoSession = (app) => {
  const session = app.storage.incognito
    ? { name: app.imageBaseName || 'Incognito (unsaved)', updatedAt: Date.now() }
    : null;
  try { app.tabs.reportIncognito(session); } catch { /* no coordinator */ }
};

// Like openImageHere's incognito branch, but keeps the conversation (the assistant's §10 `openUrl`).
export const adoptIncognitoHere = (app) => {
  if (!app.storage.incognito) app.storage.save();
  app.newEditor({ keepChat: true });
  app.storage.incognito = true;
  app.updateIncognitoUI();
};

// Create the project on the server, push layout + result, then LINK the session so it becomes a
// normal server-backed project (incognito off, still no local record). Returns the remote link.
export const publishIncognitoToServer = async (app, address) => {
  const conn = requireConnection(app.connections, address);
  if (!app.image || !app.imageDataUrl) throw new Error('Open an image first');
// Raw bytes for the codec-free server.
  const blob = await (await fetch(app.imageDataUrl)).blob();
  const bytes = new Uint8Array(await blob.arrayBuffer());
  const ext = (blob.type && blob.type.includes('/')) ? blob.type.split('/')[1] : (app.imageExt || 'png');
  const name = app.imageBaseName || 'Untitled';
  const link = await createRemoteProject(conn, {
    name, source: app.imageSource || '', resource: app.imageResource || '',
    bytes, ext,
    w: app.originalImage ? app.originalImage.width : 0,
    h: app.originalImage ? app.originalImage.height : 0,
  });
// Explicit publish, independent of the sync toggle.
  app.remoteLink = await saveRemoteProject(conn, link, {
    name,
    layout: app.currentLayoutPayload(),
    bytes: await app.remoteSync.renderResultBytes(), ext: 'png', w: app.canvas.width, h: app.canvas.height,
  });
  app.storage.incognito = false;
  app.updateButtons();
  notify(`Published to ${conn.url}`, 'ok');
  return app.remoteLink;
};

// The local twin of publishIncognitoToServer: incognito only promises the app writes
// nothing BY ITSELF, so an explicit "save this" is honoured. Null with nothing to keep.
export const promoteIncognitoToLocal = (app) => {
  if (!app.image) return null;
  app.storage.incognito = false;
  app.storage.promoteTemporaryToProject();
  app.storage.save();
  app.tabs.reportActive(app.activeProjectId);
  app.updateButtons();
  notify('Left incognito — saved as a local project', 'ok');
  return app.activeProjectId;
};
