import { observeReveal } from '../lib/motion.js';

export const listEl = document.getElementById('list');
observeReveal(listEl, '.row');
export const statusEl = document.getElementById('status');
// `.status:empty` is display:none and display cannot transition, so the fade runs
// FIRST and the text is emptied when it lands.
const STATUS_LEAVE_MS = 200;
let statusLeaveTimer = null;
export const clearStatus = () => {
  clearTimeout(statusLeaveTimer);
  if (!statusEl.textContent) return;
  statusEl.classList.add('status-leaving');
  statusLeaveTimer = setTimeout(() => {
    statusEl.classList.remove('status-leaving');
    statusEl.textContent = '';
  }, STATUS_LEAVE_MS);
};
// A new message mid-fade cancels it, so it never shows up half-gone.
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

export const THUMB_PX = 48;     // .thumb in popup.css
// A video whose frame could not be read (cross-origin).
export const PLAY_THUMB = 'data:image/svg+xml,' + encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"><rect width="48" height="48" fill="#2b2f3a"/><polygon points="19,15 35,24 19,33" fill="#7c3aed"/></svg>');

// The three host documents are told apart by path; only the popup closes after an action.
export const IS_SIDE_PANEL = location.pathname.includes('sidepanel');
export const IS_DEVTOOLS = location.pathname.includes('devtools');
export const dismiss = () => { if (!IS_SIDE_PANEL && !IS_DEVTOOLS) window.close(); };

export const run = async (fn) => {
  try {
    await fn();
  } catch (err) {
    statusEl.textContent = `Failed: ${err.message}`;
  }
};

// Stamped on <html> by lib/accent.js before first paint.
export const themePref = window.StencilTheme;
export const themeBtn = document.getElementById('theme-toggle');
