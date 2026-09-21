// The wired modal shells: opening one can close whichever other is showing, and one
// Escape listener picks the topmost.
export const modalShells = new Set();

// One Escape listener for every shell: the topmost open window answers, and only it. A shell
// may keep the key for itself; no window the app ships does (tests/modalShell.test.js).
let escapeWired = false;
export const wireEscapeOnce = () => {
  if (escapeWired) return;
  escapeWired = true;
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    const open = [...modalShells].filter((s) => s.isOpen());
    if (!open.length) return;
    // A stacked window sits over whatever raised it.
    const top = open.find((s) => s.stacked) || open[0];
    if (top.takesEscape()) top.close();
  });
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
