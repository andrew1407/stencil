import { subscribe } from '../../eventBus/appBus.js';
import { MOTION_EVENT, multiWindow } from '../motion/motionPrefs.js';

// The wired modal shells: opening one can close whichever other is showing, and one
// Escape listener picks the topmost.
export const modalShells = new Set();

// One Escape listener for every shell: the topmost open window answers, and only it. A shell
// may keep the key for itself; no window the app ships does (tests/ui/modalShell.test.js).
let escapeWired = false;
// The presses a window took: the fullscreen layer leaves the mode only on one no window answered.
const taken = new WeakSet();
export const windowTookEscape = (e) => taken.has(e);
// The last raised of `shells` (the first on a tie: none raised), null when there are none.
const lastRaised = (shells) => shells.reduce((a, s) => (!a || s.raisedAt > a.raisedAt ? s : a), null);
export const wireEscapeOnce = () => {
  if (escapeWired) return;
  escapeWired = true;
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    const open = [...modalShells].filter((s) => s.isOpen());
    if (!open.length) return;
    // A stacked window sits over whatever raised it; side by side, the last one raised is on top.
    const top = open.find((s) => s.stacked) || lastRaised(open);
    if (!top.takesEscape()) return;
    taken.add(e);
    top.close();
  });
};

// The shell wired over an overlay id: config/uiStrings.json `windows` names each window's overlay.
export const shellFor = (overlayId) => {
  for (const shell of modalShells) if (overlayId && shell.overlayId === overlayId) return shell;
  return null;
};

// Close every open modal (optionally sparing one); returns the first shell closed. A
// `stacked` window means two can be up at once.
export const closeOpenModal = (except = null) => {
  let closed = null;
  for (const shell of modalShells) {
    if (shell === except || !shell.isOpen()) continue;
    shell.close();
    closed = closed || shell;
  }
  return closed;
};

// Popover-shaped windows only: the multi-window mode keeps every full window a new one opens beside.
export const closeOpenPopovers = (except = null) => {
  for (const shell of modalShells) if (shell !== except && shell.isOpen() && shell.isPopover()) shell.close();
};

// Side-by-side windows share one band under the context menu (99999), restacked in raise order.
const SIDE_BY_SIDE_Z = 99000;
let raises = 0;
export const raiseWindow = (shell) => {
  shell.raisedAt = ++raises;
  const up = [...modalShells].filter((s) => s.isOpen() && s.raisedAt > 0 && !s.stacked)
    .sort((a, b) => a.raisedAt - b.raisedAt);
  up.forEach((s, i) => s.setZ(SIDE_BY_SIDE_Z + i + 1));
};

// Multiple windows switched off: one window stays — the one holding the focus (the Visuals window
// whose box was just unticked), else the one on top (the console's call) — and the rest close.
export const collapseToOneWindow = (doc = globalThis.document) => {
  const open = [...modalShells].filter((s) => s.isOpen());
  const full = open.filter((s) => !s.isPopover());
  const keep = full.find((s) => s.contains(doc?.activeElement)) || lastRaised(full);
  for (const s of open) if (s !== keep) s.close();
  if (keep) { keep.raisedAt = 0; keep.setZ(null); keep.holdBackdrop?.(); }
};

let collapseWired = false;
export const wireCollapseOnce = () => {
  if (collapseWired) return;
  collapseWired = true;
  let was = multiWindow();
  subscribe(MOTION_EVENT, (e) => {
    const now = !!e?.detail?.multiWindow;
    if (was && !now) collapseToOneWindow();
    was = now;
  });
};
