// ── window.stencil's session actions — new / blank / save / load ────────────
// The members that replace what the editor is holding. Each one that names a server
// validates the connection BEFORE it resets or fetches anything.
import { requireConnection } from '../net/remoteSync.js';
import { videoFrameDataUrl } from '../core/videoFrame.js';

export const createSessionApi = ({ app, connMgr }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  // loadImageFromFile decodes async with no promise; poll until the image is in place.
  // `previous` = the image loaded BEFORE the call, so a REPLACE waits for the swap — not
  // for "some image exists", which would run chained ops against the old picture.
  const waitForImage = (timeoutMs = 8000, previous = null) => new Promise((resolve) => {
    const start = Date.now();
    const again = typeof requestAnimationFrame === 'function'
      ? requestAnimationFrame : (fn) => setTimeout(fn, 16);   // node --test has no rAF
    const tick = () => {
      if ((app.image && app.image !== previous) || Date.now() - start > timeoutMs) resolve();
      else again(tick);
    };
    tick();
  });

  const api = {
    // Start a fresh blank (unsaved) editor — the toolbar's clear/new. `opts.address` also
    // creates+links an empty project on that server, so a later save() writes back.
    newEditor(opts = {}) {
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before resetting
      app.newEditor();
      if (address) return app.createRemoteBlank(address).then(() => stencil);
      return stencil;
    },
    // Create a solid-color blank image to draw on. `color` is any CSS color; opts.size =
    // { width, height } px; `opts.address` also creates+links it on that server.
    async blank(color = '#ffffff', opts = {}) {
      const size = opts.size || {};
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before replacing
      const blankOpts = { color, width: size.width, height: size.height };
      if (address) blankOpts.address = address;
      await app.createBlankImage(blankOpts);   // awaited: the swap is already done
      await waitForImage();
      return stencil;
    },
    // Save the session: a server-linked project writes back to its origin server
    // (version-guarded); a purely-local one flushes to storage. Resolves to the facade.
    save() {
      if (app.remoteLink) return app.remoteSync.saveToServer().then(() => stencil);
      app.storage.save();
      return stencil;
    },

    // Load an image (or, with `frame`/a video URL, that frame) by URL. Resolves to the
    // facade, so `(await stencil.load(url)).crop(...)` chains. `incognito: true` adopts
    // incognito IN PLACE first — the outgoing project is flushed, the tab stays put.
    async load(url, opts = {}) {
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before fetching
      const resp = await fetch(url);
      if (!resp.ok) throw new Error(`Failed to fetch ${url}: HTTP ${resp.status}`);
      const blob = await resp.blob();
      const type = blob.type || '';
      const baseName = opts.name || decodeURIComponent(url.split('/').pop().split(/[?#]/)[0] || '') || 'image';

      let file;
      if (type.startsWith('video/') || opts.frame != null || opts.usePoster) {
        // Grab a frame from the video (usePoster has no poster on a bare URL → ignored).
        const dataUrl = await videoFrameDataUrl(URL.createObjectURL(blob), Number(opts.frame) || 0);
        const fb = await (await fetch(dataUrl)).blob();
        file = new File([fb], baseName.replace(/\.[^.]+$/, '') + '.jpg', { type: 'image/jpeg' });
      } else if (type.startsWith('image/')) {
        file = new File([blob], baseName, { type });
      } else {
        throw new Error(`Not an image or video (got "${type || 'unknown'}")`);
      }

      const loadOpts = { source: opts.source ?? url, resource: opts.resource ?? '' };
      if (opts.crop) loadOpts.crop = opts.crop;
      if (address) loadOpts.address = address;   // create+link on that server after load
      const previous = app.image;
      // Adopt only once the bytes are in hand: a failed fetch must leave the editor
      // exactly as it was, never reset into an empty incognito session.
      if (opts.incognito) app.adoptIncognitoHere();
      app.loadImageFromFile(file, loadOpts);
      await waitForImage(8000, previous);
      return stencil;
    },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
