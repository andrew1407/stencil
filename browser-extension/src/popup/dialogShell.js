// The panel's dialog shell: click-away and Escape both cancel; an `anchor` switches on
// the popover shape (lib/popover.js placement). The Escape listener must be removed on
// EVERY close route, or it lingers for the panel's lifetime.
import { popoverPosition } from '../lib/tip/popover.js';
import { menuTransformOrigin } from '../lib/chat/chatMsgMenu.js';
import { surfaceIn, surfaceOut, centerOf } from '../lib/motion.js';

// Resolves exactly once; `finish(undefined)` is a cancel, as click-away / Escape are.
export const openPanelDialog = ({ build, anchor }) => new Promise((resolve) => {
  const back = document.createElement('div');
  back.className = 'dialog-back';
  const box = document.createElement('div');
  box.className = 'dialog';

  let origin = null;

  let settled = false;
  const onEscape = (e) => { if (e.key === 'Escape') finish(undefined); };
  const finish = (value) => {
    if (settled) return;
    settled = true;
    document.removeEventListener('keydown', onEscape);
    // Dusted while still on screen, removed on the same frame: nothing waits on the motes.
    surfaceOut(box, origin);
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
    // Measured after it is in the DOM: the popover class fixes the box's positioning.
    const p = popoverPosition({
      anchor: anchor.getBoundingClientRect(),
      box: box.getBoundingClientRect(),
      viewport: { width: window.innerWidth, height: window.innerHeight },
    });
    box.style.left = `${p.left}px`;
    box.style.top = `${p.top}px`;
    origin = centerOf(anchor);
    const r = box.getBoundingClientRect();
    box.style.transformOrigin = menuTransformOrigin({
      x: origin.x, y: origin.y, left: p.left, top: p.top, size: { width: r.width, height: r.height },
    });
  }
  surfaceIn(box, origin || centerOf(box));
});
