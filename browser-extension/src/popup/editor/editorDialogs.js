import { openPanelDialog } from './dialogShell.js';

// Click-away, Escape and Cancel all mean no: a destructive action needs a deliberate click.
export const confirmDialog = (titleText, subject, warning, confirmLabel, anchor) =>
  openPanelDialog({
    anchor,
    build: (finish) => {
      const title = document.createElement('div');
      title.className = 'dialog-title';
      title.textContent = titleText;
      const what = document.createElement('div');
      what.className = 'dialog-note';
      what.textContent = [subject, warning].filter(Boolean).join(' — ');

      const row = document.createElement('div');
      row.className = 'dialog-actions';
      const cancel = document.createElement('button');
      cancel.textContent = 'Cancel';
      cancel.addEventListener('click', () => finish(false));
      const ok = document.createElement('button');
      ok.className = 'primary';
      ok.textContent = confirmLabel;
      ok.addEventListener('click', () => finish(true));
      row.append(cancel, ok);
      return [title, what, row];
    },
  }).then((v) => v === true);

// Resolves the chosen ImportMode, or undefined when cancelled.
export const promptImportMode = (state, anchor) => openPanelDialog({
  anchor,
  build: (finish) => {
    const title = document.createElement('div');
    title.className = 'dialog-title';
    title.textContent = 'This editor already holds an image.';
    const what = document.createElement('div');
    what.className = 'dialog-note';
    // The names come from the editor page: text, never markup.
    what.textContent = [state.projectName || '(unnamed project)', state.imageName].filter(Boolean).join(' · ')
      + (state.incognito ? ' · not saved (incognito)' : '');

    const row = document.createElement('div');
    row.className = 'dialog-actions';
    const choice = (label, val, primary = false) => {
      const b = document.createElement('button');
      if (primary) b.className = 'primary';
      b.textContent = label;
      b.addEventListener('click', () => finish(val));
      return b;
    };
    row.append(
      choice('Add as a new project', 'new', true),
      choice('Replace the image', 'replace'),
      choice('Replace, keep the annotations', 'replace-keep'),
      choice('Cancel', undefined),
    );
    return [title, what, row];
  },
});
