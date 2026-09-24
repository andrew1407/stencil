// A logo show's notice: the gold pill with the egg and its golden shining, bottom-left, capped
// at MAX_VISIBLE and coalescing a repeat.
// Browser twin: the `shine` toast of js/ui/shell/notifications.js.
import { surfaceIn, surfaceOut } from '../../lib/motion.js';
import { attachToastGlow } from '../../lib/logo/toastGlow.js';
import { STAGE } from '../../lib/logo/stageRules.js';

// browser config/svgArt.json secretEgg, pinned by tests/options/secrets.test.js.
export const SECRET_EGG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="4.7 1.6 14.6 18.8" width="%1" height="%2"><path fill="%3" fill-rule="evenodd" d="M4.8 13.2A7.2 11.6 0 0 1 19.2 13.2A7.2 7.2 0 0 1 4.8 13.2Z M6.8 9.8L8.533 8.5L10.267 9.8L12 8.5L13.733 9.8L15.467 8.5L17.2 9.8L17.2 11.2L15.467 9.9L13.733 11.2L12 9.9L10.267 11.2L8.533 9.9L6.8 11.2Z M6.8 16L8.533 14.7L10.267 16L12 14.7L13.733 16L15.467 14.7L17.2 16L17.2 17.4L15.467 16.1L13.733 17.4L12 16.1L10.267 17.4L8.533 16.1L6.8 17.4Z M8.35 13.3a.85.85 0 1 0 1.7 0a.85.85 0 1 0-1.7 0Z M11.15 13.3a.85.85 0 1 0 1.7 0a.85.85 0 1 0-1.7 0Z M13.95 13.3a.85.85 0 1 0 1.7 0a.85.85 0 1 0-1.7 0Z"/></svg>';
const HIDE_MS = 2400;
const LEAVE_ANIM_MS = 260;   // notifyLeave in secrets.css
const ENTER_DUST_MS = 680;
const LEAVE_DUST_MS = 420;
export const MAX_VISIBLE = 3;

const column = (doc) => {
  let el = doc.getElementById('notify-balloon');
  if (!el) {
    el = doc.createElement('div');
    el.id = 'notify-balloon';
    doc.body.appendChild(el);
  }
  return el;
};
const live = (col) => [...col.children].filter((el) => !el.classList.contains('notify-leaving'));
// Past the column's left edge at the toast's own height.
const dustPoint = (toast) => {
  const r = toast.getBoundingClientRect();
  return { x: -r.width * 0.15, y: r.top + r.height / 2 };
};

const dismiss = (toast) => {
  if (!toast || toast.classList.contains('notify-leaving')) return;
  clearTimeout(toast._hideTimer);
  toast.classList.add('notify-leaving');
  surfaceOut(toast, dustPoint(toast), { ms: LEAVE_DUST_MS });
  setTimeout(() => { toast._glowStop?.(); toast.remove(); }, LEAVE_ANIM_MS);
};

export const notifyShine = (msg, doc = document) => {
  if (typeof doc?.createElement !== 'function' || !doc.body) return;
  const col = column(doc);
  const dup = live(col).find((el) => el.querySelector('.notify-text')?.textContent === msg);
  if (dup) {
    clearTimeout(dup._hideTimer);
    dup._hideTimer = setTimeout(() => dismiss(dup), HIDE_MS);
    return;
  }
  const standing = live(col);
  for (let i = 0; i < standing.length + 1 - MAX_VISIBLE; i++) dismiss(standing[i]);
  const toast = doc.createElement('div');
  toast.className = 'notify-toast notify-ok notify-shine';
  toast.setAttribute('role', 'status');
  toast.innerHTML = '<span class="notify-icon"></span><span class="notify-text"></span>';
  toast.querySelector('.notify-icon').innerHTML =
    SECRET_EGG.replaceAll('%1', '14').replaceAll('%2', '18').replaceAll('%3', STAGE.toastInk);
  // Gold whatever the accent: the aura carries the colour of the secret, never of the theme.
  toast.style.background = STAGE.toastGold;
  toast.style.color = STAGE.toastInk;
  toast.querySelector('.notify-text').textContent = msg;
  col.appendChild(toast);
  surfaceIn(toast, dustPoint(toast), { ms: ENTER_DUST_MS });
  toast._glowStop = attachToastGlow(toast, doc);
  toast._hideTimer = setTimeout(() => dismiss(toast), HIDE_MS);
};
