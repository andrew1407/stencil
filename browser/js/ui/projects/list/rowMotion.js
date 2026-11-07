// A project row's leave and arrival: under Sliding every row pours into the list's left edge and what
// arrives pours out of it (genie); Transition slides them the same way (css/components/projects/rows.css).
// Every other mode keeps the shared leave and materialize. Desktop twin: support/control/reveal/edgeFlight.
import { leaveThenRemove, LEAVE_MS } from '../../motion/enterLeave.js';
import { materialize } from '../../motion/control/chatFx.js';
import { genieAway, genieIn, settleGenie, MENU_GENIE_MS } from '../../motion/genie/window.js';
import { motionMode } from '../../motion/motionPrefs.js';

// The strip on the list's left edge, level with the row's middle (chatFx.js chatTailHome's shape).
export const leftHome = (r, view) => (r && view && r.height > 0
  ? { left: view.left + 1, top: Math.max(view.top, r.top + r.height / 2 - 8), width: 2, height: 16 }
  : null);

const viewOf = (el) => el?.parentElement?.getBoundingClientRect?.() ?? null;

// leaveThenRemove's options; resolves once the slot has closed and, poured, once the picture is home.
export function rowLeave(el, opts = {}) {
  const view = viewOf(el);
  const poured = motionMode() === 'slide'
    && genieAway(el, leftHome(el?.getBoundingClientRect?.(), view), { ms: MENU_GENIE_MS, clip: view });
  if (poured) el.style.visibility = 'hidden';   // the picture is the row now; its slot still folds
  return leaveThenRemove(el, () => {}, poured ? { ...opts, ms: Math.max(opts.ms ?? LEAVE_MS, MENU_GENIE_MS) } : opts);
}

// materialize's options, or Sliding's pour out of the left edge.
export function rowArrive(el, opts = {}) {
  const view = viewOf(el);
  const poured = motionMode() === 'slide' && genieIn(el, leftHome(el?.getBoundingClientRect?.(), view),
    { ms: MENU_GENIE_MS, clip: view, landed: () => settleGenie(el) });
  if (!poured) materialize(el, opts);
}
