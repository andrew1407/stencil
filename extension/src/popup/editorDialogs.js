// ── The editor panel's two ask-first dialogs ────────────────────────────────
// Both ride the shared shell (dialogShell.js) and hold no state of their own.
import { openPanelDialog } from './dialogShell.js';

// A yes/no dialog on the shared shell (dialogShell.js): click-away, Escape and Cancel
// all mean no, so a destructive action needs a deliberate click on its own button.
// With an `anchor` it opens as the popover pinned next to the asking control.
export const confirmDialog = (titleText, subject, warning, confirmLabel, anchor) =>
  openPanelDialog({
    anchor,
    build: (finish) => {
      const title = document.createElement('div');
      title.className = 'dialog-title';
      title.textContent = titleText;
      const what = document.createElement('div');
      what.className = 'dialog-note';
      what.textContent = [subject, warning].filter(Boolean).join(' — ');   // page data → text

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
  }).then((v) => v === true);   // undefined (click-away / Escape) reads as "no"

// The occupied-editor chooser, on the shared shell. Resolves the chosen ImportMode, or
// undefined when cancelled (click-away / Escape). With an `anchor` it opens as a
// popover next to the clicked row, so the image being imported stays in view.
export const promptImportMode = (state, anchor) => openPanelDialog({
  anchor,
  build: (finish) => {
    const title = document.createElement('div');
    title.className = 'dialog-title';
    title.textContent = 'This editor already holds an image.';
    const what = document.createElement('div');
    what.className = 'dialog-note';
    // The project + image names come from the editor page → text, never markup.
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
