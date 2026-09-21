// The wrappers the editor-mode handlers are built from: answer-exactly-once, the
// privileged-sender gate, and the timeboxed ask-one-editor-tab leg.
import { getSettings } from '../lib/stencil.js';
import { isEditorTab } from '../lib/menu/editorTabs.js';
import { MSG } from '../lib/messages.js';

// How long a leg waiting on the editor PAGE gets before it's declared silent (an older editor
// build never replies). The fan-out is parallel, so listing N editors costs one deadline.
const PAGE_ANSWER_MS = 1500;
const NO_PAGE_ANSWER = 'the editor page did not answer';

// Wrap an async handler into a request/response one: returns true (holds the port open) and
// answers EXACTLY once, turning a throw into the `{ ok:false, error }` object callers branch on.
export const answers = (fn) => (msg, sender, sendResponse) => {
  Promise.resolve()
    .then(() => fn(msg, sender))
    .catch((err) => ({ ok: false, error: err?.message || String(err) }))
    .then((res) => { try { sendResponse(res); } catch { /* port closed — the panel went away */ } });
  return true;
};

// chrome.tabs.get for a tab that may have closed (or an id that came in as data): null
// instead of a throw.
export const getTab = async (tabId) => {
  try { return await chrome.tabs.get(tabId); } catch { return null; }
};

// Who may ask for the privileged, cross-tab handlers. Our own pages are identified by
// ORIGIN, not a missing `sender.tab` (popup.html can open as an ordinary tab).
const senderMayRelayPrivileged = async (sender) => {
  if (!sender) return false;
  const from = sender.url || '';
  if (from.startsWith(chrome.runtime.getURL(''))) return true;   // popup / side panel / options
  const { editorUrl, editorPageApi } = await getSettings();
  if (!editorPageApi) return false;
  return isEditorTab(from || sender.tab?.url || '', editorUrl);
};

// Composed inside answers(): answers(privileged(fn)).
export const privileged = (fn) => async (msg, sender) => {
  if (!(await senderMayRelayPrivileged(sender))) {
    return { ok: false, error: 'this request is not allowed from a page' };
  }
  return fn(msg, sender);
};

// Ask ONE editor tab's bridge something, always returning a reply object: no receiver and a
// silent page both become the same structured error, never a hang.
export const askEditorTab = async (tabId, message) => {
  try {
    const reply = await Promise.race([
      chrome.tabs.sendMessage(tabId, message),
      new Promise((resolve) => setTimeout(resolve, PAGE_ANSWER_MS)),
    ]);
    return reply && typeof reply === 'object' ? reply : { ok: false, error: NO_PAGE_ANSWER };
  } catch {
    return { ok: false, error: NO_PAGE_ANSWER };
  }
};

// One parallel EDITOR_STATE fan-out over candidate tabs. Canvas capture is the expensive
// part of a state reply, so `thumbnail` defaults off; callers that want previews say so.
export const probeEditorTabs = (tabs, { thumbnail = false } = {}) =>
  Promise.all(tabs.map((tab) => (tab.id == null
    ? null
    : askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail }))));

// Which tab a message means: an explicit tabId (the panel picked one), else the sender's —
// for the editor page's own API, "no tabId" means "the tab I'm standing in".
export const targetTabId = (msg, sender) => (typeof msg.tabId === 'number' ? msg.tabId : sender?.tab?.id);

// The destination editor tab: it must still exist AND still be on the editor origin (a tab
// listed a moment ago may have navigated away). Returns `{tab}` or `{error}`.
export const editorTabFor = async (msg, sender) => {
  const tabId = targetTabId(msg, sender);
  if (typeof tabId !== 'number') return { error: 'no tab' };
  const tab = await getTab(tabId);
  if (!tab) return { error: 'no such tab' };
  const { editorUrl } = await getSettings();
  if (!isEditorTab(tab.url || '', editorUrl)) return { error: 'tab is not a Stencil editor' };
  return { tab };
};
