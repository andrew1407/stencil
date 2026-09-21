// Popup / side panel / DevTools panel: ./scan.js reads the page, ./filters.js ranks it,
// ./row.js draws it; this file owns the controls' wiring and the boot order.
import { setSettings } from '../lib/stencil.js';
import { highlightColorValue } from '../lib/highlight/highlightColor.js';
import { toggleStencilHighlight } from '../lib/highlight/highlight.js';
import { icon } from '../lib/icons.js';
import { MSG } from '../lib/messages.js';
import { watchNumericInputs } from '../lib/control/numericInput.js';
import { initTooltips } from '../lib/tip/controlTooltip.js';
import { wireScrollbarHover } from '../lib/control/scrollbarHover.js';
import { setTip } from '../lib/tip/tip.js';
import { enhanceSelect } from '../lib/control/customSelect.js';
import { statusEl, themePref, themeBtn, IS_SIDE_PANEL, IS_DEVTOOLS } from './panelDom.js';
import { state } from './list/model.js';
import { filterUi, applyFilters } from './list/filters.js';
import { scan, annotateOpened } from './list/scan.js';
import { syncServerFilterUI } from './pin/sharedPins.js';
import { runHoverHighlight, highlightListRowForSource } from './row/hoverHighlight.js';
import './row/row.js';
import './pin/pinActions.js';
import './dragWiring.js';
import './list/sections.js';
import './editor/editorSection.js';
import './assistantControls.js';
import './storageSync.js';

let searchTimer = null;
document.getElementById('f-search').addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
});
['f-regex', 'f-img', 'f-bg', 'f-video', 'f-poster', 'f-meta'].forEach(id => document.getElementById(id).addEventListener('change', applyFilters));

// The toggles persist to settings so they stay in sync with the options page.
document.getElementById('f-mark-opened').addEventListener('change', async (e) => {
  await setSettings({ markOpened: e.target.checked });
  await annotateOpened();
  applyFilters();
});
document.getElementById('f-opened-first').addEventListener('change', async (e) => {
  await setSettings({ openedFirst: e.target.checked });
  state.openedFirst = e.target.checked;
  applyFilters();
});
document.getElementById('f-show-pinned').addEventListener('change', async (e) => {
  await setSettings({ showPinned: e.target.checked });
  state.showPinned = e.target.checked;
  applyFilters();
});
// Hover highlight gates both directions (row→page element, page element→row).
document.getElementById('f-hover-hl').addEventListener('change', async (e) => {
  state.hoverHighlight = e.target.checked;
  await setSettings({ hoverHighlight: e.target.checked });
  if (!e.target.checked) { runHoverHighlight(''); highlightListRowForSource(''); }
});
document.getElementById('f-server-pins').addEventListener('change', () => { syncServerFilterUI(); applyFilters(); });
document.getElementById('f-server-store').addEventListener('change', applyFilters);

document.getElementById('f-highlight').addEventListener('change', async (e) => {
  if (state.activeTabId == null) { e.target.checked = false; return; }
  try {
    const color = await highlightColorValue();
    await chrome.scripting.executeScript({
      target: { tabId: state.activeTabId, allFrames: true }, func: toggleStencilHighlight, args: [e.target.checked, color]
    });
  } catch (err) {
    statusEl.textContent = `Couldn’t toggle highlight (${err.message}).`;
    e.target.checked = false;
  }
});
document.getElementById('f-fmt-toggle').addEventListener('click', () => {
  const target = !filterUi.allChecked();
  filterUi.checkboxes().forEach(c => { c.checked = target; });
  filterUi.updateToggleLabel();
  applyFilters();
});

['f-minw', 'f-maxw', 'f-minh', 'f-maxh'].forEach(id => document.getElementById(id).addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
}));
document.getElementById('rescan').addEventListener('click', scan);
// Computed up front: after an extension reload an open DevTools panel is invalidated
// (every chrome.* touch throws) but window.open on this URL still works.
const optionsUrl = (() => {
  try { return chrome.runtime.getURL('src/options/options.html'); } catch { return null; }
})();
document.getElementById('open-options').addEventListener('click', () => {
  // The DevTools panel's context has no chrome.runtime.openOptionsPage.
  try {
    if (typeof chrome.runtime.openOptionsPage === 'function') { chrome.runtime.openOptionsPage(); return; }
    chrome.runtime.sendMessage({ type: MSG.OPEN_OPTIONS });
  } catch {
    if (optionsUrl) window.open(optionsUrl, '_blank');
  }
});

// The toggle pins the opposite of what is PAINTED, so the first click flips the view
// even while the mode is still 'system'.
if (themePref && themeBtn) {
  const syncThemeBtn = () => {
    const dark = themePref.resolved() === 'dark';
    themeBtn.innerHTML = icon(dark ? 'sun' : 'moon');
    setTip(themeBtn, dark ? 'Switch to the light theme' : 'Switch to the dark theme', { label: true });
  };
  themeBtn.addEventListener('click', () => {
    themePref.set(themePref.resolved() === 'dark' ? 'light' : 'dark', themeBtn);
    syncThemeBtn();
  });
  themePref.onChange(syncThemeBtn);
  syncThemeBtn();
}

// Popup only. Opening a side panel needs a user gesture, which this click is.
document.getElementById('open-sidepanel')?.addEventListener('click', async () => {
  try {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    await chrome.sidePanel.open({ windowId: tab.windowId });
    window.close();
  } catch (err) {
    statusEl.textContent = `Couldn’t open the side panel (${err.message}).`;
  }
});

// The side panel outlives a page, so it re-scans on tab switch; the popup scans once.
if (IS_SIDE_PANEL) {
  chrome.tabs.onActivated.addListener(() => scan());
  chrome.tabs.onUpdated.addListener((_id, info, tab) => {
    if (tab.active && info.status === 'complete') scan();
  });
} else if (IS_DEVTOOLS) {
  chrome.devtools.network.onNavigated.addListener(() => scan());
}

watchNumericInputs();

filterUi.load().then(() => { filterUi.restoreStatic(); scan(); });

initTooltips();
wireScrollbarHover();

// In a 400px window the OS select list covers the page.
for (const el of document.querySelectorAll('select')) enhanceSelect(el);
