// ── Background service worker ───────────────────────────────────────────────
// Owns the right-click context menu (Stencil actions → editor). Covers real <img>
// (native 'image' context) and CSS background-image elements (detected by the
// content-script probe, ctxTarget.js).
// This file is wiring only: it builds the menu, registers the content scripts, and
// routes runtime messages / menu clicks to the modules beside it.
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

// React to settings changes: re-scope the editor bridge + editor page API (editorUrl), and
// toggle the page API (exposeWindowStencil) / the editor page API (editorPageApi). Collected
// into one pass, so a change that re-scopes two sets is still a single settings read.
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

// Also set up on every worker start (onInstalled/onStartup don't fire on every wake):
// re-assert all three registrations; only the bridge also injects into open tabs.
setUpScripts({ inject: ['bridge'] });
applyAccentActionIcon();   // tint the toolbar icon's outline to the saved accent
watchAccentActionIcon();   // …and re-tint it whenever the accent changes

// ── Runtime-message dispatch ────────────────────────────────────────────────
// One handler per message `type` (keyed by MSG.*). Two kinds: fire-and-forget handlers
// return undefined (port closes), request/response ones (the editor-mode group) are
// wrapped in `answers()` and return true. The listener just propagates that.
const messageHandlers = {
  ...pageApiHandlers, ...dropZoneHandlers, ...ctxProbeHandlers, ...editorModeHandlers,
};

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const handler = msg && messageHandlers[msg.type];
  if (handler) return handler(msg, sender, sendResponse);   // the return value gates the port
});

chrome.contextMenus.onClicked.addListener((info, tab) => resolveClickHandler(info)(info, tab, tab?.id));
