import { scatterGridFor, surfaceIn, surfaceOut, centerOf } from '../lib/motion.js';

// Stands in for window.confirm() (the options page has no native modal). Resolves true on
// Yes/Enter, false on No/Esc/backdrop. `anchor` is the button the particles fly out of and back into.
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

// A CLEAR is destructive: the rows scatter (shared mesh budget) and leave the DOM before the rebuild.
