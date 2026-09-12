// The session side of the .stencil file (projectFile.js is the pure (de)serializer): gather
// the state, apply a parsed file, prompt on a live-sync conflict, paint the live-sync button.
import { validateLayout, mergeLines } from './layout.js';
import { isAccent } from './accents.js';

// The shape projectFile.buildProjectFile wants; JSON/IO live in ExportService + projectFile.js.
export const projectFileState = (app, { includeTheme = true } = {}) => {
  const meta = (app.activeProjectId != null && app.storage.store.getMeta(app.activeProjectId)) || {};
  const state = {
    name: meta.name || app.imageBaseName || 'Untitled',
    color: meta.color || '',
    keywords: Array.isArray(meta.keywords) ? meta.keywords : [],
    source: app.imageSource || '',
    resource: app.imageResource || '',
    blank: !!app.blankColor,
    blankColor: app.blankColor || '',
    layout: app.currentLayoutPayload(),
  };
  if (app.imageDataUrl) {
    state.image = {
      dataUrl: app.imageDataUrl,          // the ORIGINAL (crop/rotation live in layout)
      ext: app.imageExt || 'png',
      w: app.originalImage ? app.originalImage.width : 0,
      h: app.originalImage ? app.originalImage.height : 0,
    };
  }
  if (includeTheme) state.theme = { mode: app.theme, accent: app.customAccent || app.accent };
  return state;
};

// Apply a parsed .stencil as a NEW local project via the server-reopen path; the theme only
// if the file carried one. Returns the project name.
export const applyProjectFile = async (app, project) => {
  if (!project || !project.image || !project.image.dataUrl) throw new Error('Project file has no image');
  const name = project.name || 'Untitled';
  const blob = await (await fetch(project.image.dataUrl)).blob();
  const ext = project.image.ext || 'png';
  const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
  // Mirror openImageHere: flush current, reset, then load.
  if (!app.storage.incognito) app.storage.save();
  app.newEditor();
  app.loadImageFromFile(file, {
    name,
    source: project.source || '',
    resource: project.resource || '',
    layout: project.layout,
    adoptLayout: true,
    color: project.color || '',
    keywords: project.keywords || [],
    blankColor: project.blank ? (project.blankColor || '') : undefined,
    fromFile: true,   // mark provenance so the projects list shows a bronze .stencil outline
  });
  // Theme is a global setting: only a file that carries one may change it.
  const t = project.theme;
  if (t) {
    if (t.mode) app.setTheme(t.mode);
    if (t.accent) { if (isAccent(t.accent)) app.setAccent(t.accent); else app.setCustomAccent(t.accent); }
  }
  return name;
};

// Live file sync: update the CURRENT project's layout in place via the co-edit adopt path,
// optionally union-merging lines on a conflict.
export const applyProjectFileInPlace = (app, project, opts = {}) => {
  if (!project || !app.image) return;
  const layout = project.layout || {};
  const verdict = validateLayout(layout, {
    hasImage: !!app.image, imgW: app.canvas.width, imgH: app.canvas.height,
    hasExistingLines: !!(app.lines && app.lines.length),
  });
  if (!verdict.ok) return;
  app.lines = opts.mergeLines ? mergeLines(verdict.lines, app.lines) : verdict.lines;
  if (Number.isInteger(layout.rotationQuarters)) app.rotationQuarters = layout.rotationQuarters;
  if (layout.cropRect) app.cropRect = app.imageModel.roundRect(layout.cropRect);
  app.imageModel.rebuildCroppedImage();
  app.remoteSync.adoptServerFilter(layout);
  app.remoteSync.adoptServerFormulas(layout);
  app.remoteSync.adoptServerPageFormat(layout);
  app.currentLine = null;
  app.history.reset(app.lines);
  app.zoomPan.fitToWindow();
  app.updateInfo();
  app.coordTable.update(app.lines.length > 0 ? app.lines[app.lines.length - 1].points : null);
  app.renderer.redraw();
  app.updateButtons();
  app.storage.save();
};

// 3-way live-sync conflict prompt, two confirms so it reuses the existing modal → 'theirs' | 'merge' | 'mine'.
export const chooseFileConflict = async (app, name = '.stencil') => {
  if (await app.confirm(
    `“${name}” was changed outside the app and conflicts with your unsaved edits. Reload the file’s version (discard yours)?`,
    { title: 'File changed', confirmLabel: 'Take file’s version', confirmIcon: 'download', cancelLabel: 'Keep / merge…' })) {
    return 'theirs';
  }
  return (await app.confirm(
    'Merge instead — combine your lines with the file’s?',
    { title: 'Merge changes', confirmLabel: 'Merge both', confirmIcon: 'layers', cancelLabel: 'Keep mine (overwrite file)' }))
    ? 'merge' : 'mine';
};

export const updateStencilSyncUI = (app) => {
  const btn = document.getElementById('live-sync-btn');
  if (!btn) return;
  const s = app.stencilSync;
  const on = s.supported && s.linked && s.liveSync;
  btn.classList.toggle('active', on);
  btn.disabled = !(s.supported && s.linked);
  // controlTooltip's data-disabled-reason: the markup default is the "not linked" case.
  btn.dataset.disabledReason = s.supported ? 'Open or save a .stencil file first'
    : 'Live file sync needs a Chromium browser (File System Access API)';
  btn.dataset.title = !s.supported ? 'Live file sync needs a Chromium browser (File System Access API)'
    : !s.linked ? 'Open or save a .stencil file first to enable live sync'
      : on ? `Live sync ON — auto-saving to ${s.name} and watching it for changes`
        : `Live sync OFF — click to auto-save to ${s.name} and watch it for changes`;
  // Delete needs a retained file handle.
  const del = document.getElementById('delete-project-btn');
  if (del) {
    del.disabled = !s.linked;
    del.dataset.title = s.linked ? `Delete “${s.name}” from disk (the project stays open here)`
      : 'Open or save a .stencil file first';
  }
};

