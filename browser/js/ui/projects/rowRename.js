import { icon } from '../icons.js';
import { wireNameEditor } from '../../utils.js';
import { validateProjectName } from '../../core/parse/validation.js';
import { markIn, markOut } from '../motion.js';

// The row's inline rename editor: ✓/✗ form out of dust, live validation against the store's
// uniqueness rule, Enter = ✓, Escape / click-away = ✗. `gesture` is the row's own open gesture,
// which the two clicks that opened this editor had already armed.
export function beginRowRename({ meta, name, app, render, gesture }) {
  gesture?.cancel();
  const wrap = document.createElement('span');
  wrap.className = 'project-rename-wrap';
  const input = document.createElement('input');
  input.className = 'project-name-edit';
  input.type = 'text';
  input.value = meta.name || 'Untitled';
  input.dataset.title = 'Project name';
  const accept = document.createElement('button');
  accept.type = 'button';
  accept.className = 'name-edit-btn name-edit-accept';
  accept.innerHTML = icon('check', { size: 14 });
  accept.dataset.title = 'Save name (Enter)';
  const cancel = document.createElement('button');
  cancel.type = 'button';
  cancel.className = 'name-edit-btn name-edit-cancel';
  cancel.innerHTML = icon('x', { size: 14 });
  cancel.dataset.title = 'Cancel (Esc)';
  wrap.append(input, accept, cancel);
  // The editor is INSIDE the row, and the row opens the project on click — so a press on ✓/✗
  // reached it and closed the window (user report). Nothing inside the editor is a row click.
  for (const ev of ['mousedown', 'click', 'dblclick'])
    wrap.addEventListener(ev, (e) => e.stopPropagation());
  name.replaceWith(wrap);
  // ✓/✗ FORM from dust (desktop revealControls parity); their hover already
  // draws the check / strikes the cross (animations/iconHover.css .ic-check/.ic-x).
  markIn(accept);
  markIn(cancel);
  input.focus();
  input.select();
  let done = false;
  const finish = (save, next) => {
    if (done) return;
    done = true;
    // …and come apart BEFORE the re-render sweeps the editor away — the
    // clouds are copies on <body>, so the rebuild never waits for them.
    markOut(accept);
    markOut(cancel);
    // renameProject re-checks uniqueness; adopt the name only if accepted.
    if (save && next && next !== meta.name && app.renameProject(meta.id, next)) meta.name = next;
    render();
  };
  // Live-validated ✓/✗ (always shown here): ✓ enabled only for a changed, valid
  // name, its tooltip explaining any rejection. Enter = ✓, Escape/click-away = ✗.
  wireNameEditor(input, accept, cancel, {
    alwaysShow: true,
    current: () => meta.name || '',
    validate: (v) => validateProjectName(app.storage.store, v, meta.id),
    commit: (v) => finish(true, v),
    cancel: () => finish(false),
  });
  input.addEventListener('keydown', e => e.stopPropagation());   // keep modal hotkeys out
  input.addEventListener('blur', () => finish(false));           // click-away discards
  input.addEventListener('click', e => e.stopPropagation());
}
