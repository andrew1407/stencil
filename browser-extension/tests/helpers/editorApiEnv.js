// The stub editor page the editorApi*.test.js suites run src/content/editorApiMain.js against: a
// MAIN-world IIFE with no exports, so its posted envelopes are answered by hand.

export const SRC = { EXT_API: 'stencil-ext-api', EXT_API_RES: 'stencil-ext-api-res' };
export const MSG = { EDITOR_LIST: 'stencil-editor-list', EDITOR_STATE: 'stencil-editor-state', EDITOR_IMPORT: 'stencil-editor-import', EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', EDITOR_FOCUS_TAB: 'stencil-editor-focus-tab', SOURCE_TABS: 'stencil-source-tabs', SCAN_TAB: 'stencil-scan-tab', PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop' };

export const setupEnv = ({ extPreset } = {}) => {
  const posted = [];
  const listeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') listeners.push(fn); },
    postMessage: (m) => posted.push(m),
  };
  // The API's handler checks `e.source === window` (= win) — mimic a real same-window message.
  const dispatch = (data, source = win) => { for (const fn of listeners) fn({ source, data }); };
  if (extPreset !== undefined) win.__stencilExt = extPreset;
  globalThis.window = win;
  return { posted, win, dispatch };
};

let caseId = 0;
// Fresh stub page + a fresh module evaluation; returns { ext, posted, win, dispatch }.
export const loadApi = async (opts) => {
  const env = setupEnv(opts);
  await import(`../../src/content/editorApiMain.js?case=${++caseId}`);
  return { ext: env.win.__stencilExt, ...env };
};

// The call envelopes the API posted at the bridge.
export const calls = (posted) => posted.filter((p) => p.source === SRC.EXT_API);
export const lastCall = (posted) => calls(posted).at(-1);
// Answer the pending call the way the bridge does: the SW's response, id-correlated.
export const answer = (env, result) => {
  const c = lastCall(env.posted);
  env.dispatch({ source: SRC.EXT_API_RES, id: c.id, ok: true, result });
  return c.message;
};
export const refuse = (env, error, result) => {
  const c = lastCall(env.posted);
  env.dispatch({ source: SRC.EXT_API_RES, id: c.id, ok: false, error, result });
  return c.message;
};

export const scanRow = (src, extra = {}) => ({ kind: 'img', src, w: 800, h: 600, ...extra });
