// The `#stencil=` hand-off payload: a server reference for a linked session (the receiver
// re-fetches — no bytes, no token), else the inline image plus the full layout.

const launchPayload = ({ remote, dataUrl, name, layout, source, resource, incognito }) => {
  if (remote) {
    const p = { server: { url: remote.url, id: remote.id, version: remote.version || 0 } };
    if (incognito) p.incognito = true;
    return p;
  }
  const p = { dataUrl, name, layout };
  if (source) p.source = source;
  if (resource) p.resource = resource;
  if (incognito) p.incognito = true;
  return p;
};

// The saved-project half: read out of the registry instead of the live editor.
const storedLaunchPayload = (app, id, incognito) => {
  const meta = app.storage.store.getMeta(id);
  const proj = app.storage.store.get(id);
  if (!meta || !proj) return null;
  const layout = proj.payload?.layout || {};
  return launchPayload({
    remote: meta.remoteId && meta.address
      && { url: meta.address, id: meta.remoteId, version: meta.remoteVersion },
    dataUrl: proj.payload?.image || null,
    name: `${layout.imageBaseName || meta.name || 'image'}.${meta.imageExt || layout.imageExt || 'png'}`,
    layout,
    source: meta.source || layout.imageSource,
    resource: meta.resource || layout.imageResource,
    incognito,
  });
};

// `id` hands off a SAVED project instead of the open one; null when nothing is stored.
export const openInLaunchPayload = (app, { incognito = false, id = null } = {}) => {
  if (id != null && id !== app.activeProjectId) return storedLaunchPayload(app, id, incognito);
  return launchPayload({
    remote: app.remoteLink
      && { url: app.remoteLink.address, id: app.remoteLink.remoteId, version: app.remoteLink.version },
    dataUrl: app.imageDataUrl,
    name: `${app.imageBaseName || 'image'}.${app.imageExt || 'png'}`,
    layout: app.currentLayoutPayload(),
    source: app.imageSource,
    resource: app.imageResource,
    incognito,
  });
};
