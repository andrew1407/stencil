// Background service worker: wiring only. Owns the right-click context menu (real <img>
// and CSS background-image via the probe, ctxTarget.js) and routes messages/clicks to
// the modules beside it.
import { applyAccentActionIcon, watchAccentActionIcon } from '../lib/actionIcon.js';
import { buildMenus, syncDesktopMenuVisibility } from './menus.js';
import { injectProbeIntoOpenTabs, setUpScripts } from './registrars.js';
import { resolveClickHandler } from './ctxActions.js';
import { pageApiHandlers } from './handlers/pageApi.js';
import { dropZoneHandlers } from './handlers/dropZones.js';
import { ctxProbeHandlers } from './handlers/ctxProbe.js';
import { editorModeHandlers } from './handlers/editorMode.js';
import './tabState.js';   // per-tab probe state + the pins snapshot, kept fresh from storage

// Build immediately on worker startup (covers reloads where onInstalled/onStartup
// don't fire); idempotent thanks to the removeAll above.
buildMenus();

// React to settings changes: re-scope the editor bridge/page APIs, collected into one pass
// so a change touching two sets is still a single settings read.
chrome.storage.onChanged.addListener((changes, area) => {
  if (area !== 'sync') return;
  const keys = [];
  if (changes.editorUrl) keys.push('bridge');
  if (changes.editorUrl || changes.editorPageApi) keys.push('editorApi');
  if (changes.exposeWindowStencil) keys.push('pageApi');
  if (keys.length) setUpScripts({ keys });
  if (changes.desktopScheme) syncDesktopMenuVisibility();   // reveal/hide the desktop-app items
});

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
applyAccentActionIcon();   // tint the toolbar icon's outline to the saved accent
watchAccentActionIcon();   // …and re-tint it whenever the accent changes

// One handler per message `type`: fire-and-forget ones return undefined (port closes);
// request/response ones are wrapped in `answers()` and return true, which this propagates.
const messageHandlers = {
  ...pageApiHandlers, ...dropZoneHandlers, ...ctxProbeHandlers, ...editorModeHandlers,
};

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const handler = msg && messageHandlers[msg.type];
  if (handler) return handler(msg, sender, sendResponse);   // the return value gates the port
});

chrome.contextMenus.onClicked.addListener((info, tab) => resolveClickHandler(info)(info, tab, tab?.id));
