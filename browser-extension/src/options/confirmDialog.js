import { surfaceIn, surfaceOut, centerOf, genieIn, genieOut, growIn, growOut, settleGenie } from '../lib/motion.js';

const BOX = `
  <div class="confirm-box" role="alertdialog" aria-modal="true" aria-labelledby="confirm-msg">
    <div class="confirm-msg" id="confirm-msg"></div>
    <div class="confirm-actions">
      <button id="confirm-no" class="confirm-btn" type="button">No</button>
      <button id="confirm-yes" class="confirm-btn danger" type="button">Yes</button>
    </div>
  </div>`;

// Built on the first confirm, then kept hidden at the end of <body> like any other overlay.
const overlayEl = () => {
  const found = document.getElementById('confirm-overlay');
  if (found) return found;
  const overlay = document.createElement('div');
  overlay.id = 'confirm-overlay';
  overlay.className = 'confirm-overlay';
  overlay.hidden = true;
  overlay.innerHTML = BOX;
  document.body.append(overlay);
  return overlay;
};

// Stands in for window.confirm() (the options page has no native modal). Resolves true on
// Yes/Enter, false on No/Esc/backdrop. `anchor` is the button the particles fly out of and back into.
export const confirmDialog = (message, anchor) => new Promise((resolve) => {
  const overlay = overlayEl();
  const box = overlay.querySelector('.confirm-box');
  settleGenie(box);   // a close still in the air hides the overlay now, not over this open
  const origin = centerOf(anchor);
  document.getElementById('confirm-msg').textContent = message;
  const yes = document.getElementById('confirm-yes');
  const no = document.getElementById('confirm-no');
  const done = (val) => {
    // Dusted while still on screen and hidden on this frame; Sliding hides it as it lands.
    const hide = () => { overlay.hidden = true; };
    if (surfaceOut(box, origin) || !(genieOut(box, anchor, hide, overlay) || growOut(box, anchor, hide, overlay))) hide();
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
  if (!surfaceIn(box, origin) && !genieIn(box, anchor)) growIn(box, anchor);
  yes.focus();
});

// A CLEAR is destructive: the rows scatter (shared mesh budget) and leave the DOM before the rebuild.
