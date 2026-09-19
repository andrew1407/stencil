import { wireNameEditor } from '../../utils.js';
import { markIn, markOut } from '../motion.js';
import { validateProjectName } from '../../core/validation.js';
export function wireProjectNameField(app) {
  const nameInput = document.getElementById('project-name-input');
  const nameEdit = document.getElementById('project-name-edit');
  const nameAccept = document.getElementById('project-name-accept');
  const nameCancel = document.getElementById('project-name-cancel');
  if (nameInput && nameAccept && nameCancel) {
    const currentName = () => (app.activeProjectId != null ? (app.storage.store.getMeta(app.activeProjectId)?.name || '') : '');
    // Only a saved (non-incognito) project can be renamed.
    const canRename = () => app.activeProjectId != null && !app.storage.incognito;
    const endEdit = () => {
      app.nameEditing = false;
      nameInput.readOnly = true;
      // ✓/✗ come apart as dust — photographed before the display flip hides them.
      markOut(nameAccept);
      markOut(nameCancel);
      nameAccept.style.display = 'none';
      nameCancel.style.display = 'none';
      app.updateProjectTitle(true);   // restore value + ✎ visibility
    };
    const beginEdit = () => {
      if (!canRename() || app.nameEditing) return;
      app.nameEditing = true;
      nameInput.readOnly = false;
      if (nameEdit) nameEdit.style.display = 'none';
      const colorBtn = document.getElementById('project-color-btn');
      if (colorBtn) colorBtn.style.display = 'none';
      nameAccept.style.display = '';
      nameCancel.style.display = '';
      // …and FORM from dust once shown (the projects-modal rename editor's twin).
      markIn(nameAccept);
      markIn(nameCancel);
      app.nameEditor?.refresh();     // set ✓ enabled/disabled for the starting value
      nameInput.focus();
      nameInput.select();
    };
    app.nameEditor = wireNameEditor(nameInput, nameAccept, nameCancel, {
      alwaysShow: true,                // edit-mode controls ✓/✗ visibility, not change-detection
      current: currentName,
      validate: (v) => validateProjectName(app.storage.store, v, app.activeProjectId),
      commit: (v) => {
        if (app.activeProjectId != null) app.renameProject(app.activeProjectId, v);   // syncs imageBaseName itself
        endEdit();
      },
      cancel: () => endEdit(),
    });
    nameInput.addEventListener('dblclick', () => beginEdit());
    if (nameEdit) nameEdit.addEventListener('click', () => beginEdit());
    // ✎/🎨 hover-reveal as sand (desktop parity: setPaintedOut) — the .name-hover class flips
    // their visibility and the mark dust makes the flip read as forming.
    {
      const field = nameInput.closest('.project-name-field');
      const revealable = () => [nameEdit, document.getElementById('project-color-btn')]
        .filter((b) => b && b.style.display !== 'none');
      if (field) {
        field.addEventListener('mouseenter', () => {
          field.classList.add('name-hover');
          for (const b of revealable()) markIn(b, { ms: 160 });
        });
        field.addEventListener('mouseleave', () => {
          for (const b of revealable()) markOut(b, { ms: 120 });   // photographed before the class hides them
          field.classList.remove('name-hover');
        });
      }
    }
    // A real click-away (the ✓/✗ buttons prevent their own mousedown, so they don't
    // blur) discards the in-progress rename.
    nameInput.addEventListener('blur', () => { if (app.nameEditing) endEdit(); });
  }
}
