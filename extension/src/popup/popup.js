// ── Popup: list, filter, and act on every image on the active page ───────────
// The panel is assembled here: ./scan.js reads the page, ./filters.js ranks it,
// ./row.js draws it, and the action modules act on a row. This file owns the
// controls' wiring and the boot order.
import { setSettings } from '../lib/stencil.js';
import { highlightColorValue } from '../lib/highlightColor.js';
import { toggleStencilHighlight } from '../lib/highlight.js';
import { icon } from '../lib/icons.js';
import { MSG } from '../lib/messages.js';
import { watchNumericInputs } from '../lib/numericInput.js';
import { initTooltips } from '../lib/controlTooltip.js';
import { wireScrollbarHover } from '../lib/scrollbarHover.js';
import { setTip } from '../lib/tip.js';
import { enhanceSelect } from '../lib/customSelect.js';
import { statusEl, themePref, themeBtn, IS_SIDE_PANEL, IS_DEVTOOLS } from './panelDom.js';
import { state } from './model.js';
import { filterUi, applyFilters } from './filters.js';
import { scan, annotateOpened } from './scan.js';
import { syncServerFilterUI } from './sharedPins.js';
import { runHoverHighlight, highlightListRowForSource } from './hoverHighlight.js';
import './row.js';
import './pinActions.js';
import './dragWiring.js';
import './sections.js';
import './editorSection.js';
import './assistantControls.js';
import './storageSync.js';

// ── Wiring ──
let searchTimer = null;
document.getElementById('f-search').addEventListener('input', () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(applyFilters, 150);
});
['f-regex', 'f-img', 'f-bg', 'f-video', 'f-poster', 'f-meta'].forEach(id => document.getElementById(id).addEventListener('change', applyFilters));

// Opened-images toggles (persisted to settings so they follow the user and stay in
// sync with the options page). "mark opened" needs a re-annotate (badges depend on
// it); "opened first" only re-sorts the current list.
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
// Show-pinned toggle: styles pinned rows (gray outline) and floats them to the top.
// Persisted (follows the user + options page); pinning still works when it's off.
document.getElementById('f-show-pinned').addEventListener('change', async (e) => {
  await setSettings({ showPinned: e.target.checked });
  state.showPinned = e.target.checked;
  applyFilters();
});
// Highlight-on-hover toggle: gates BOTH directions (row→page element, page element→row).
// Persisted (follows the user + the other open surfaces). Turning it off clears any outline
// already showing on the page and any outlined row.
document.getElementById('f-hover-hl').addEventListener('change', async (e) => {
  state.hoverHighlight = e.target.checked;
  await setSettings({ hoverHighlight: e.target.checked });
  if (!e.target.checked) { runHoverHighlight(''); highlightListRowForSource(''); }
});
// Server-pins filter: toggle visibility of server-stored items + per-server narrowing.
document.getElementById('f-server-pins').addEventListener('change', () => { syncServerFilterUI(); applyFilters(); });
document.getElementById('f-server-store').addEventListener('change', applyFilters);

// Highlight toggle: outline every grabbable element on the page. Off by default.
document.getElementById('f-highlight').addEventListener('change', async (e) => {
  if (state.activeTabId == null) { e.target.checked = false; return; }
  try {
    // All frames, so iframed content is highlighted too.
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
// Cached while the context is alive: after an extension reload an already-open
// DevTools panel is INVALIDATED — every chrome.* touch throws — but a plain
// window.open on this pre-computed URL still lands in the fresh extension.
const optionsUrl = (() => {
  try { return chrome.runtime.getURL('src/options/options.html'); } catch { return null; }
})();
document.getElementById('open-options').addEventListener('click', () => {
  // The DevTools panel reuses this script, and its context has no
  // chrome.runtime.openOptionsPage — route through the service worker there.
  try {
    if (typeof chrome.runtime.openOptionsPage === 'function') { chrome.runtime.openOptionsPage(); return; }
    chrome.runtime.sendMessage({ type: MSG.OPEN_OPTIONS });
  } catch {
    // Invalidated context (the extension reloaded under this panel): best
    // effort — open the options page as an ordinary tab, never throw.
    if (optionsUrl) window.open(optionsUrl, '_blank');
  }
});

// Dark / light toggle (mirrors the editor's moon button). It pins the opposite of
// what's PAINTED, so the first click flips what you see even while the mode is still
// 'system'; Options offers the full System / Light / Dark choice.

if (themePref && themeBtn) {
  const syncThemeBtn = () => {
    const dark = themePref.resolved() === 'dark';
    themeBtn.innerHTML = icon(dark ? 'sun' : 'moon');
    setTip(themeBtn, dark ? 'Switch to the light theme' : 'Switch to the dark theme', { label: true });
  };
  themeBtn.addEventListener('click', () => {
    // Pass the button itself, so the palette floods out of it and never has to guess.
    themePref.set(themePref.resolved() === 'dark' ? 'light' : 'dark', themeBtn);
    syncThemeBtn();
  });
  themePref.onChange(syncThemeBtn);
  syncThemeBtn();
}

// Popup only: promote this view into the docked side panel (same UI, but it persists
// while you work the page and re-scans on tab switch). Opening a side panel needs a
// user gesture, which this click is; closing the popup hands focus to the panel.
document.getElementById('open-sidepanel')?.addEventListener('click', async () => {
  try {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    await chrome.sidePanel.open({ windowId: tab.windowId });
    window.close();
  } catch (err) {
    statusEl.textContent = `Couldn’t open the side panel (${err.message}).`;
  }
});

// Side panel only: it outlives a single page, so re-scan when the user switches tabs
// or the active tab finishes loading new content (the popup, which closes on blur,
// just scans once on open). Guard re-entrancy isn't needed — scan() resets state.
if (IS_SIDE_PANEL) {
  chrome.tabs.onActivated.addListener(() => scan());
  chrome.tabs.onUpdated.addListener((_id, info, tab) => {
    if (tab.active && info.status === 'complete') scan();
  });
} else if (IS_DEVTOOLS) {
  // A DevTools panel is pinned to one tab and never switches; it only needs to
  // re-scan when that inspected page navigates to fresh content.
  chrome.devtools.network.onNavigated.addListener(() => scan());
}

// Load the persisted filters first, restore the static controls, then scan (the pill
// rebuild restores the format toggles from the same persisted state).
// Numeric fields (the size filters) take an expression — "45 + 9", "* 2".
watchNumericInputs();

filterUi.load().then(() => { filterUi.restoreStatic(); scan(); });

// Instant, structured tooltips everywhere on this page (the native `title` waits ~1s
// and never shows on a disabled control). lib/tipContent.js gives them their shape.
initTooltips();
wireScrollbarHover();   // every scrollable's thumb takes the accent under the pointer

// Same for the popup's own filter selects — in a 400px window the OS list covers the page.
for (const el of document.querySelectorAll('select')) enhanceSelect(el);
