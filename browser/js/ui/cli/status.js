// The status line under the logo — the Zig console's `image: …` line, the name in the project's
// colour — and the gestures on the name: a click shows the editing picture, a double-click the
// original, Alt+hover peeks, Shift+Alt+hover peeks it magnified.
import { statusLine } from '../../console/shell/status.js';
import { onAltKeys, offAltKeys, pointerIn } from '../tip/altPeek.js';

export const paintStatus = (el, app) => {
  const s = statusLine(app);
  const doc = el.ownerDocument || globalThis.document;
  el.textContent = '';
  if (!s.name) { el.textContent = s.text; return s; }
  const at = s.text.indexOf(s.name);
  el.appendChild(doc.createTextNode(s.text.slice(0, at)));
  const name = doc.createElement('span');
  name.className = 'cli-status-name';
  name.textContent = s.name;
  if (s.color) name.style.setProperty('--name-color', s.color); else name.style.removeProperty('--name-color');
  el.appendChild(name);
  el.appendChild(doc.createTextNode(s.text.slice(at + s.name.length)));
  return s;
};

// The name is the control: one click toggles the editing picture, a double-click the original,
// and Alt held over it peeks the editing picture while the key is down (Shift too: magnified).
export const wireStatusGestures = (el, peek, { clickMs = 220, schedule = (fn, ms) => setTimeout(fn, ms), cancel = clearTimeout } = {}) => {
  let timer = null;
  const nameOf = (e) => e.target?.closest?.('.cli-status-name');
  el.addEventListener('click', (e) => {
    if (!nameOf(e)) return;
    if (timer) { cancel(timer); timer = null; return; }
    timer = schedule(() => { timer = null; peek.toggle('editing'); }, clickMs);
  });
  el.addEventListener('dblclick', (e) => {
    if (!nameOf(e)) return;
    if (timer) { cancel(timer); timer = null; }
    peek.toggle('original');
  });
  const alt = {
    press: (e, inHit) => { if (inHit(el.querySelector?.('.cli-status-name') ?? el)) peek.hold('editing', { magnified: !!e.shiftKey }); },
    release: () => peek.release(),
  };
  onAltKeys(alt);
  // Shift pressed or let go while the peek is held magnifies it or not.
  const doc = el.ownerDocument || globalThis.document;
  const shift = (e) => { if (e.key === 'Shift' && peek.isHeld?.()) peek.magnify(e.type === 'keydown'); };
  doc.addEventListener('keydown', shift);
  doc.addEventListener('keyup', shift);
  return { over: () => pointerIn(el), dispose: () => { offAltKeys(alt); doc.removeEventListener('keydown', shift); doc.removeEventListener('keyup', shift); } };
};
