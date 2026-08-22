// ── Shared panel-dialog shell ───────────────────────────────────────────────
// The one place the panel's dialogs (pin-target picker, the occupied-editor import
// chooser, the close-editor confirm) get their chrome: the `.dialog-back` overlay, the
// box, click-away and Escape both cancelling, and — when the caller passes the control
// that triggered it — the POPOVER shape: the same dialog pinned next to that control
// (lib/popover.js placement, the browser app's rules) with a transparent backdrop, so
// the list stays readable behind it. No anchor = the classic centred dimmed dialog.
// The Escape listener must be removed on EVERY close route (finish), not only when
// Escape itself fires — otherwise it lingers for the panel's lifetime.
import { popoverPosition } from '../lib/popover.js';

/**
 * Open a dialog shell and resolve it exactly once.
 * @param {object} args
 * @param {(finish: (value: any) => void) => (Node[]|Node)} args.build - Build the dialog's
 *   content; `finish(value)` closes and settles the promise (undefined = cancelled).
 * @param {Element} [args.anchor] - The control that opened it → the anchored popover shape.
 * @returns {Promise<any>} The value passed to finish; undefined on click-away / Escape.
 */
export const openPanelDialog = ({ build, anchor }) => new Promise((resolve) => {
  const back = document.createElement('div');
  back.className = 'dialog-back';
  const box = document.createElement('div');
  box.className = 'dialog';

  let settled = false;
  const onEscape = (e) => { if (e.key === 'Escape') finish(undefined); };
  const finish = (value) => {
    if (settled) return;   // one settle, no matter how many close routes fire
    settled = true;
    document.removeEventListener('keydown', onEscape);
    back.remove();
    resolve(value);
  };

  const content = build(finish);
  box.append(...(Array.isArray(content) ? content : [content]));
  back.appendChild(box);
  back.addEventListener('click', (e) => { if (e.target === back) finish(undefined); });
  document.addEventListener('keydown', onEscape);
  document.body.appendChild(back);

  if (anchor?.getBoundingClientRect) {
    back.classList.add('dialog-popover');
    // Measure AFTER it is in the DOM (the popover class fixes the box's positioning).
    const p = popoverPosition({
      anchor: anchor.getBoundingClientRect(),
      box: box.getBoundingClientRect(),
      viewport: { width: window.innerWidth, height: window.innerHeight },
    });
    box.style.left = `${p.left}px`;
    box.style.top = `${p.top}px`;
  }
});
