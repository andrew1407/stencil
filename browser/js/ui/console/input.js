// The prompt line, as the Zig line editor reads it: Enter runs, Tab completes the first word, Up
// and Down recall, the line being typed coming back once Down passes the newest entry.
export const wireConsoleInput = (input, { run, complete, history, classify = () => '' }) => {
  let stash = null;
  // The word being typed wears its colour: the accent for a command, gold for a secret.
  const tint = () => { input.dataset.word = classify(input.value); };
  input.addEventListener('input', tint);
  input.addEventListener('keyup', tint);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      const line = input.value;
      input.value = '';
      stash = null;
      tint();
      run(line);
    } else if (e.key === 'Tab') {
      e.preventDefault();
      const next = complete(input.value);
      if (next !== null) input.value = next;
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (stash === null) stash = input.value;
      if (history.entries.length) input.value = history.up();
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      const recalled = history.down();
      if (history.atEnd()) { input.value = stash ?? ''; stash = null; } else input.value = recalled;
    }
  });
};
