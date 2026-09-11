// ── Topbar project name + the image-info line ───────────────────
// Extracted from drawingApp.js — the DOM half of "what project is open": tab title, name
// field (colour, rename affordance, remote badge) and the size/incognito/blank readout.
import { icon } from './icons.js';

// Reflect the active project's name in the tab title AND topbar field. Field editable only
// with a saved active project; shows the image-derived name for a fresh one (see projectsStore
// meta init). `force` re-syncs even while focused (commit/cancel); default respects focus so
// updateButtons() can't clobber a name being typed.
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
    // Shrink-wrap the field to the name so what follows it (the rename controls and the
    // "?" bubble) sits beside the text, not at the end of a fixed slot. Editing keeps a
    // roomier box so a longer name can be typed without the field jumping per keystroke.
    input.size = Math.max(8, Math.min(30, (app.nameEditing ? 24 : name.length) || 10));
    // `editable` (a saved, non-incognito project) only gates the rename affordance;
    // the field itself stays a read-only title until the user enters edit mode.
    input.disabled = !editable;
    if (!app.nameEditing) input.readOnly = true;
    input.placeholder = editable ? 'Untitled' : (app.storage.incognito ? 'Incognito (unsaved)' : 'No project');
    if (editBtn && !app.nameEditing) editBtn.style.display = editable ? '' : 'none';
    app.nameEditor?.refresh();                      // set ✓ enabled/disabled state
  }
  // Paint the name field in the project's custom colour; unset falls back to the CSS
  // neutral grey. Show the colour swatch only for a saved (non-incognito) project.
  if (input) {
    const projColor = (editable && app.activeProjectId != null)
      ? (app.storage.store.getMeta(app.activeProjectId)?.color || '')
      : '';
    // The legibility shadow is left ENTIRELY to CSS (--project-name-shadow) so it re-flips
    // live on theme toggle — setting it inline would freeze it to the paint-time theme.
    input.style.color = projColor || '';
    input.style.textShadow = '';
    const colorBtn = document.getElementById('project-color-btn');
    if (colorBtn && !app.nameEditing) {
      colorBtn.style.display = editable ? '' : 'none';
      // The chip is accent-FILLED, so its glyph is white like the pencil's beside it when
      // the project has no colour of its own — muted grey on the accent read as disabled
      // (user report, with a picture). A project WITH a colour still wears it: that is
      // what the control says.
      colorBtn.style.color = projColor || '#fff';
    }
  }
  // Outside edit mode (no project, incognito, post-commit, click-away) the ✓/✗
  // rename controls must never linger — they belong to edit mode only.
  if (!app.nameEditing) {
    const a = document.getElementById('project-name-accept');
    const c = document.getElementById('project-name-cancel');
    if (a) a.style.display = 'none';
    if (c) c.style.display = 'none';
  }
  // Server-editing indicator: a golden badge by the name + a golden outline on the
  // canvas, so it's obvious this session is editing a project stored on a server.
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
  // A blank project writes "· blank" into the size readout + reveals the recolour swatch.
  const isBlank = app.activeIsBlank();
  if (app.image) {
    const blankTag = isBlank ? '  ·  blank' : '';
    // Just the image size — the shortcut hints live in the "?" popup (hints-btn), not this bar.
    info.textContent = `Image Size: ${app.canvas.width} × ${app.canvas.height} px${blankTag}`;
  } else {
    info.textContent = 'No image loaded. Upload an image to start.';
  }
  // Announce incognito here, beside the image facts — the "?" bubble is about the IMAGE,
  // so an empty incognito editor had nowhere else to say it. data-size keeps that bubble
  // reading the size line alone.
  info.dataset.size = info.textContent;
  if (app.storage.incognito) {
    // A muted divider, and it exists only when the tag it separates does — so the
    // line reads "Image Size: … px | (glyph) Incognito — not saved" in incognito and has
    // no dangling bar otherwise. Decoration, so it is hidden from assistive tech.
    const sep = document.createElement('span');
    sep.className = 'info-divider';
    sep.setAttribute('aria-hidden', 'true');
    sep.textContent = '|';
    const tag = document.createElement('span');
    tag.className = 'info-incognito';
    // The app's OWN incognito glyph — the one the toolbar toggle wears — not an
    // emoji: it renders identically on every platform and, drawn with
    // stroke="currentColor", takes the tag's accent colour for free.
    tag.innerHTML = `${icon('incognito', { size: 13 })}<span>Incognito — not saved</span>`;
    info.append(sep, tag);
  }
  // Blank-fill recolour swatch: sits beside the size pill, shown only for a blank project.
  if (blankBtn) blankBtn.style.display = (app.image && isBlank) ? 'inline-flex' : 'none';
  if (blankSwatch && isBlank) blankSwatch.style.background = app.blankColor || '#ffffff';
};

