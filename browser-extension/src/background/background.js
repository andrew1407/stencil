// Background service worker: wiring only. Owns the right-click context menu (real <img>
// and CSS background-image via the probe, ctxTarget.js) and routes messages/clicks to
// the modules beside it.
import { applyAccentActionIcon, watchAccentActionIcon } from '../lib/control/actionIcon.js';
import { buildMenus } from './menus.js';
import { injectProbeIntoOpenTabs, setUpScripts, watchScriptSettings } from './registrars.js';
import { resolveClickHandler } from './ctxActions.js';
import { pageApiHandlers } from './handlers/pageApi.js';
import { dropZoneHandlers } from './handlers/dropZones.js';
import { ctxProbeHandlers } from './handlers/ctxProbe.js';
import { editorModeHandlers } from './handlers/editorMode.js';
import { storeWriteHandlers } from './handlers/storeWrites.js';
import { lockSessionKeyArea } from '../llm/sessionKey.js';
import './tabState.js';   // per-tab probe state + the pins snapshot, kept fresh from storage

// Build immediately on worker startup (covers reloads where onInstalled/onStartup
// don't fire); idempotent because buildMenus clears the menu first.
buildMenus();
watchScriptSettings();

const bootstrap = () => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpScripts();
  applyAccentActionIcon();
};
chrome.runtime.onInstalled.addListener(bootstrap);
chrome.runtime.onStartup.addListener(bootstrap);

// Re-assert on every worker start too (onInstalled/onStartup miss some wakes); only the
// bridge also injects into open tabs.
setUpScripts({ inject: ['bridge'] });
lockSessionKeyArea();      // the anthropic session key stays out of every content script's reach
applyAccentActionIcon();   // tint the toolbar icon's outline to the saved accent
watchAccentActionIcon();   // …and re-tint it whenever the accent changes

// One handler per message `type`: fire-and-forget ones return undefined (port closes);
// request/response ones are wrapped in `answers()` and return true, which this propagates.
const messageHandlers = {
  ...pageApiHandlers, ...dropZoneHandlers, ...ctxProbeHandlers, ...editorModeHandlers, ...storeWriteHandlers,
};

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const handler = msg && messageHandlers[msg.type];
  if (handler) return handler(msg, sender, sendResponse);   // the return value gates the port
});

chrome.contextMenus.onClicked.addListener((info, tab) => resolveClickHandler(info)(info, tab, tab?.id));
