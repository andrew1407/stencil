// The stub page the pageApi*.test.js suites run src/content/pageApiMain.js against: it has no
// exports, so a stub DOM/window goes on globalThis and the IIFE is imported for its side effect.

// A stub <img>: scan() reads currentSrc / getAttribute('src'); entryDims reads naturalWidth.
export const img = (url, { naturalWidth = 100, naturalHeight = 80 } = {}) => ({
  nodeType: 1, tagName: 'IMG', currentSrc: url, naturalWidth, naturalHeight,
  offsetWidth: naturalWidth, offsetHeight: naturalHeight,
  getAttribute: (a) => (a === 'src' ? url : null),
  setAttribute() {}, removeAttribute() {},
});
// A stub element carrying a CSS background-image (resolved via getComputedStyle below).
export const bg = (url, { offsetWidth = 300, offsetHeight = 150 } = {}) => ({
  nodeType: 1, tagName: 'DIV', _bg: url, offsetWidth, offsetHeight,
  getAttribute: () => null, setAttribute() {}, removeAttribute() {},
});
// A stub <video>: poster declared, no decodable frame (videoWidth 0) → poster is used.
export const video = (src, poster, { videoWidth = 0, videoHeight = 0 } = {}) => ({
  nodeType: 1, tagName: 'VIDEO', currentSrc: src, src, videoWidth, videoHeight,
  readyState: 0, paused: true, currentTime: 0,
  getAttribute: (a) => (a === 'poster' ? poster : a === 'src' ? src : null),
  setAttribute() {}, removeAttribute() {},
});

// Install a stub page on globalThis. Returns the captured postMessage payloads and a
// `dispatch` that delivers a window 'message' to the API (as the ISOLATED bridge would).
export const setupEnv = ({ imgs = [], bgs = [], videos = [], stencilPreset } = {}) => {
  const posted = [];
  const listeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') listeners.push(fn); },
    postMessage: (m) => posted.push(m),
  };
  // The API's handler checks `e.source === window` (= win) — mimic a real same-window message.
  const dispatch = (data) => { for (const fn of listeners) fn({ source: win, data }); };
  if (stencilPreset !== undefined) win.stencil = stencilPreset;
  globalThis.window = win;
  globalThis.location = { href: 'http://page.example/here' };
  // Resolve a background URL only for the elements we tagged with _bg.
  globalThis.getComputedStyle = (el) => ({ backgroundImage: el && el._bg ? `url("${el._bg}")` : '' });
  const all = [...imgs, ...videos, ...bgs];
  globalThis.document = {
    querySelectorAll: (sel) => (sel === 'img' ? imgs : sel === 'video' ? videos : sel === '*' ? all : []),
    getElementById: () => null,
    createElement: () => ({ getContext: () => null, style: {}, setAttribute() {} }),
    head: { appendChild() {} },
    documentElement: { appendChild() {} },
  };
  return { posted, win, dispatch };
};

let caseId = 0;
// A fresh module evaluation — the unique ?case= busts the ESM cache.
export const importApi = () => import(`../../src/content/pageApiMain.js?case=${++caseId}`);

// Fresh stub page + a fresh module evaluation; returns { stencil, posted, win, dispatch }.
export const loadApi = async (opts) => {
  const env = setupEnv(opts);
  await importApi();
  return { stencil: env.win.stencil, ...env };
};

export const SRC = { PAGE_API: 'stencil-page-api', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited' };
export const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin', PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
// Posted API messages. The IIFE fires one PAGE_REQUEST_SYNC on load (asking the bridge to
// push current state); filter it out so tests assert on the messages their actions caused.
export const sent = (posted) => posted.filter((p) => p.source === SRC.PAGE_API).map((p) => p.message).filter((m) => m.type !== MSG.PAGE_REQUEST_SYNC);
