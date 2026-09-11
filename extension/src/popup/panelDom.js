// ── The panel's own DOM: the element handles, the status line, and the rules that
// tell the ephemeral popup from the docked side panel / DevTools panel.
import { observeReveal } from '../lib/motion.js';

export const listEl = document.getElementById('list');
// Rows fade + lift through the list as it scrolls, and each rebuild's rows are
// picked up by the observer itself — applyFilters() stays untouched.
observeReveal(listEl, '.row');
export const statusEl = document.getElementById('status');
// Clearing the status line is the one "toast disappearing" moment this surface has.
// `.status:empty` is display:none and display can't transition, so the fade runs
// FIRST and the text is emptied when it lands. A new message mid-fade cancels it.
const STATUS_LEAVE_MS = 200;
let statusLeaveTimer = null;
export const clearStatus = () => {
  clearTimeout(statusLeaveTimer);
  if (!statusEl.textContent) return;         // already empty — nothing to play out
  statusEl.classList.add('status-leaving');
  statusLeaveTimer = setTimeout(() => {
    statusEl.classList.remove('status-leaving');
    statusEl.textContent = '';
  }, STATUS_LEAVE_MS);
};
// Any new message cancels a pending fade, so it never shows up already half-gone.
const observeStatusWrites = new MutationObserver(() => {
  if (statusEl.textContent && statusEl.classList.contains('status-leaving')) {
    clearTimeout(statusLeaveTimer);
    statusEl.classList.remove('status-leaving');
  }
});
observeStatusWrites.observe(statusEl, { childList: true, characterData: true, subtree: true });
export const countEl = document.getElementById('count');
export const previewEl = document.getElementById('preview');
export const previewImg = previewEl.querySelector('img');
export const menuEl = document.getElementById('action-menu');

export const THUMB_PX = 48;     // rendered thumbnail size (see .thumb in popup.css)
// Placeholder thumbnail for a video whose frame couldn't be read (cross-origin).
export const PLAY_THUMB = 'data:image/svg+xml,' + encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"><rect width="48" height="48" fill="#2b2f3a"/><polygon points="19,15 35,24 19,33" fill="#7c3aed"/></svg>');

// This controller drives three surfaces — the toolbar popup, the docked side panel, and
// the DevTools panel — told apart by the host document's path. The docked ones persist
// and re-scan; only the popup closes after an action.
export const IS_SIDE_PANEL = location.pathname.includes('sidepanel');
export const IS_DEVTOOLS = location.pathname.includes('devtools');
// The popup is the only ephemeral surface; the docked ones persist, so leave them.
export const dismiss = () => { if (!IS_SIDE_PANEL && !IS_DEVTOOLS) window.close(); };

// Every row / menu action runs through here: a failure lands on the status line.
export const run = async (fn) => {
  try {
    await fn();
  } catch (err) {
    statusEl.textContent = `Failed: ${err.message}`;
  }
};

// The theme + accent preferences are stamped on <html> by lib/accent.js before first
// paint; the header's moon/sun button and the assistant's §8 ops ride this handle.
export const themePref = window.StencilTheme;
export const themeBtn = document.getElementById('theme-toggle');
