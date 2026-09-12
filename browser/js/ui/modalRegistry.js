// ── The set of wired modal shells, and Escape over it ───────────────────────
// ui/modalShell.js registers each window as it wires, so opening one can close
// whichever other is showing, and ONE Escape listener can pick the topmost.
export const modalShells = new Set();

// ONE Escape listener for every shell, rather than one per shell arbitrating with the
// rest: the TOPMOST open window answers, and only it, so a single press can never take a
// stacked window and the one it was raised from together. A shell wired with
// `escapeClose: false` (settingsModal) keeps its FULL modal open — its own capture-phase
// listener owns Escape while a hotkey is being rebound — though the popover shape always
// closes. Registered on the first wired shell, where the per-shell listeners used to go.
let escapeWired = false;
export const wireEscapeOnce = () => {
  if (escapeWired) return;
  escapeWired = true;
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    const open = [...modalShells].filter((s) => s.isOpen());
    if (!open.length) return;
    // A stacked window sits OVER whatever raised it, so it is the one on top.
    const top = open.find((s) => s.stacked) || open[0];
    if (top.takesEscape()) top.close();
  });
};

// Close whatever full/popover modal is open (optionally sparing one). Returns the first
// shell closed, so callers can tell "I replaced something" from "nothing was open". Every
// open shell goes: a `stacked` window means two can be up at once.
export const closeOpenModal = (except = null) => {
  let closed = null;
  for (const shell of modalShells) {
    if (shell === except || !shell.isOpen()) continue;
    shell.close();
    closed = closed || shell;
  }
  return closed;
};
