// ── window.stencil's server connections ──────────────────────────────────────
// Thin passes to the session's ConnectionManager (net/connectionManager.js) and to the
// app's own move/copy paths. The manager itself is built once, in stencilApi.js.
export const createConnectApi = ({ app, connMgr }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  const api = {
    // ── Server connections ──
    // Connect one or more collaboration servers for this session. Accepts a URL string,
    // { url, token }, or an array of either; resolves to the facade for chaining.
    //   await stencil.connect(['a:8090', { url: 'b:8090', token: 't' }])
    async connect(urlOrUrls) { await connMgr.connect(urlOrUrls); return stencil; },
    // Close one connection by URL, or (no arg) the most recently opened one.
    disconnect(url) { connMgr.disconnect(url); return stencil; },
    // Re-establish the last connected set (re-validates/re-issues tokens).
    async reconnect() { await connMgr.reconnect(); return stencil; },
    // Read-only list of connected server URLs.
    get connections() { return connMgr.urls; },
    // Aggregated remote projects across every connection (each tagged remote:true
    // with its serverUrl). Resolves to an array of metadata records.
    serverProjects() { return connMgr.remoteProjects(); },
    // Move/copy a SERVER project (a record from serverProjects(), shape { serverUrl, id, … })
    // to local storage, or copy it into an incognito session (opts.newTab opens a new tab).
    moveServerProjectToLocal(meta) { return app.moveProjectToLocal(meta); },
    copyServerProjectToLocal(meta, opts = {}) { return app.copyServerProjectToLocal(meta, opts); },
    copyServerProjectToIncognito(meta, opts = {}) { return app.copyServerProjectToIncognito(meta, opts); },
    // Publish the current incognito session to a server (becomes a normal server project).
    publishIncognito(address) { return app.publishIncognitoToServer(address); },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
