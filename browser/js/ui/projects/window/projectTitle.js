// Topbar project name + the image-info line.
import { icon } from '../../icons.js';

// Editable only with a saved active project. `force` re-syncs even while focused
// (commit/cancel); the default respects focus so updateButtons() can't clobber typing.
export const updateProjectTitle = (app, force = false) => {
  let name = '';
  let editable = false;
  if (app.storage.incognito) {
    name = 'Incognito';
  } else if (app.activeProjectId != null) {
    name = app.storage.store.getMeta(app.activeProjectId)?.name || app.imageBaseName || 'Untitled';
    editable = true;
  } else if (app.image) {
    name = app.imageBaseName || 'Untitled';
  }
  document.title = name ? `${name} — Stencil` : 'Stencil';
  const input = document.getElementById('project-name-input');
  const editBtn = document.getElementById('project-name-edit');
  if (input && (force || document.activeElement !== input)) {
    input.value = name;
    // Shrink-wrap the field to the name; editing keeps a roomier box.
    input.size = Math.max(8, Math.min(30, (app.nameEditing ? 24 : name.length) || 10));
    // `editable` only gates the rename affordance; the field stays read-only until edit mode.
    input.disabled = !editable;
    if (!app.nameEditing) input.readOnly = true;
    input.placeholder = editable ? 'Untitled' : (app.storage.incognito ? 'Incognito (unsaved)' : 'No project');
    if (editBtn && !app.nameEditing) editBtn.style.display = editable ? '' : 'none';
    app.nameEditor?.refresh();
  }
  // Unset falls back to the CSS neutral grey; the swatch shows only for a saved project.
  if (input) {
    const projColor = (editable && app.activeProjectId != null)
      ? (app.storage.store.getMeta(app.activeProjectId)?.color || '')
      : '';
    // The legibility shadow is CSS-only (--project-name-shadow) so it re-flips on theme toggle.
    input.style.color = projColor || '';
    input.style.textShadow = '';
    const colorBtn = document.getElementById('project-color-btn');
    if (colorBtn && !app.nameEditing) {
      colorBtn.style.display = editable ? '' : 'none';
      // The chip is accent-filled, so an uncoloured project's glyph is white like the pencil's.
      colorBtn.style.color = projColor || '#fff';
    }
  }
  // Outside edit mode the ✓/✗ rename controls must never linger.
  if (!app.nameEditing) {
    const a = document.getElementById('project-name-accept');
    const c = document.getElementById('project-name-cancel');
    if (a) a.style.display = 'none';
    if (c) c.style.display = 'none';
  }
  // A golden badge by the name + a golden outline on the canvas.
  const remote = app.remoteLink;
  const badge = document.getElementById('project-remote-badge');
  if (badge) {
    badge.style.display = remote ? 'inline-flex' : 'none';
    if (remote) badge.dataset.title = `Editing a project stored on ${remote.address}`;
  }
  const canvasViewport = document.getElementById('canvas-viewport');
  if (canvasViewport) canvasViewport.classList.toggle('remote-editing', !!remote);
};

export const updateInfo = (app) => {
  const info = document.getElementById('image-info');
  const blankBtn = document.getElementById('blank-color-btn');
  const blankSwatch = document.getElementById('blank-color-swatch');
  const isBlank = app.activeIsBlank();
  if (app.image) {
    const blankTag = isBlank ? '  ·  blank' : '';
    // The shortcut hints live in the "?" popup (hints-btn), not this bar.
    info.textContent = `Image Size: ${app.canvas.width} × ${app.canvas.height} px${blankTag}`;
  } else {
    info.textContent = 'No image loaded. Upload an image to start.';
  }
  // data-size keeps the "?" bubble reading the size line alone.
  info.dataset.size = info.textContent;
  if (app.storage.incognito) {
    // The divider exists only with the tag it separates. Decoration, hidden from assistive tech.
    const sep = document.createElement('span');
    sep.className = 'info-divider';
    sep.setAttribute('aria-hidden', 'true');
    sep.textContent = '|';
    const tag = document.createElement('span');
    tag.className = 'info-incognito';
    // The app's own glyph, not an emoji: identical on every platform, and it takes the
    // tag's accent colour via stroke="currentColor".
    tag.innerHTML = `${icon('incognito', { size: 13 })}<span>Incognito — not saved</span>`;
    info.append(sep, tag);
  }
  if (blankBtn) blankBtn.style.display = (app.image && isBlank) ? 'inline-flex' : 'none';
  if (blankSwatch && isBlank) blankSwatch.style.background = app.blankColor || '#ffffff';
};

