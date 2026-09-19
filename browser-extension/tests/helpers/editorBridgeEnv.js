// The stub editor page the editorBridge*.test.js suites run src/content/editorBridge.js against:
// an ISOLATED-world IIFE with no exports, so stubs go on globalThis and it is imported per ?case=.
import { installChromeStub } from './chromeStub.js';

export const SRC = { EXT_REQ: 'stencil-ext-req', EXT_RES: 'stencil-ext-res', EXT_API: 'stencil-ext-api', EXT_API_RES: 'stencil-ext-api-res' };
export const MSG = { EDITOR_STATE: 'stencil-editor-state', EDITOR_IMPORT: 'stencil-editor-import', EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', EDITOR_LIST: 'stencil-editor-list' };

// `swResponse` is what chrome.runtime.sendMessage resolves with (undefined = no receiver).
export const setupEnv = ({ swResponse, editorPageApi = true } = {}) => {
  const posted = [];
  const messageListeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') messageListeners.push(fn); },
    postMessage: (m) => posted.push(m),
  };
  // Deliver a window 'message' as the editor app's postMessage would (same-window source).
  const dispatch = (data, source = win) => { for (const fn of messageListeners) fn({ source, data }); };
  globalThis.window = win;
  globalThis.localStorage = { getItem: () => null };   // no registry → empty publishRegistry
  // The page-API relay rides on the editorPageApi setting (see editorBridge.js): the
  // bridge reads it (sync storage) on load and follows changes.
  const stub = installChromeStub({
    sync: { editorPageApi },
    respond: () => (swResponse === undefined ? undefined : Promise.resolve(swResponse)),
  });
  // Deliver a chrome.runtime message as the SW / panel would, collecting the sendResponse
  // answers and each listener's return value (true = "I'll answer asynchronously").
  const send = (msg) => {
    const replies = [];
    const kept = stub.runtimeListeners.map((fn) => fn(msg, {}, (r) => replies.push(r)));
    return { replies, kept };
  };
  return { sent: stub.sent, posted, win, dispatch, send };
};

let caseId = 0;
export const loadBridge = async () => { await import(`../../src/content/editorBridge.js?case=${caseId++}`); };
// Let the bridge's promise chains settle (a round-trip is a couple of microtasks).
export const flush = () => new Promise((r) => setImmediate(r));
// The EXT_REQ envelopes the bridge posted at the editor page.
export const pageReqs = (posted) => posted.filter((p) => p.source === SRC.EXT_REQ);
// The EXT_API_RES envelopes the bridge posted back at the MAIN-world API.
export const apiRes = (posted) => posted.filter((p) => p.source === SRC.EXT_API_RES);
