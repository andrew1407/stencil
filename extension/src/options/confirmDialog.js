// ── Themed Yes/No confirmation ──────────────────────────────────────────────
import { scatterGridFor, surfaceIn, surfaceOut, centerOf } from '../lib/motion.js';

// Themed Yes/No confirmation. The options page has no native modal of its own, so this
// stands in for window.confirm() and matches the editor's look (theme.css vars). Resolves
// true on Yes/Enter, false on No/Esc/backdrop click.
// `anchor` is the button it was raised from: the box's particles fly out of that button
// and stream back into it, the same as every menu here.
export const confirmDialog = (message, anchor) => new Promise((resolve) => {
  const overlay = document.getElementById('confirm-overlay');
  const box = overlay.querySelector('.confirm-box');
  const origin = centerOf(anchor);
  document.getElementById('confirm-msg').textContent = message;
  const yes = document.getElementById('confirm-yes');
  const no = document.getElementById('confirm-no');
  const done = (val) => {
    surfaceOut(box, origin);   // dusted while still on screen, hidden on this frame
    overlay.hidden = true;
    yes.removeEventListener('click', onYes);
    no.removeEventListener('click', onNo);
    overlay.removeEventListener('mousedown', onBackdrop);
    document.removeEventListener('keydown', onKey, true);
    resolve(val);
  };
  const onYes = () => done(true);
  const onNo = () => done(false);
  const onBackdrop = (e) => { if (e.target === overlay) done(false); };
  const onKey = (e) => {
    if (e.key === 'Escape') { e.stopPropagation(); done(false); }
    else if (e.key === 'Enter') { e.preventDefault(); done(true); }
  };
  yes.addEventListener('click', onYes);
  no.addEventListener('click', onNo);
  overlay.addEventListener('mousedown', onBackdrop);
  document.addEventListener('keydown', onKey, true);
  overlay.hidden = false;
  surfaceIn(box, origin);
  yes.focus();
});

// A CLEAR is destructive too, so the listed rows scatter rather than fading like a
// filter — all at once, on the shared mesh budget (scatterGridFor), and out of the DOM
// before the rebuild.
