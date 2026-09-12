// ── The .stencil project file: save, open, pick and delete ─────────────────
// Uses the File System Access pickers where they exist (and keeps the handle, so the
// project can live-sync to that file), falling back to a download / transient <input>.
import { notify, shortName } from '../utils.js';
import { arriveFrom } from '../ui/motion.js';
import { serializeProjectFile, parseProjectFile } from './projectFile.js';

// ── .stencil project file: whole-project save/open (image + layout + metadata + optional theme) ──
// Saves via the File System Access Save-As dialog when supported, else the download-blob fallback.
export const saveProjectFile = async (svc, { includeTheme = true } = {}) => {
  const app = svc.app;
  if (!app.image || !app.imageDataUrl) { notify('Open an image first', 'fail'); return; }
  const text = serializeProjectFile(app.projectFileState({ includeTheme }));
  const base = (app.storage.store.getMeta(app.activeProjectId)?.name || app.imageBaseName || 'project')
    .replace(/[/\\?%*:|"<>]/g, '-').trim() || 'project';
  const filename = `${base}.stencil`;
  try {
    if (window.showSaveFilePicker) {
      const handle = await window.showSaveFilePicker({
        suggestedName: filename,
        types: [{ description: 'Stencil project', accept: { 'application/x-stencil': ['.stencil'] } }],
      });
      const writable = await handle.createWritable();
      await writable.write(text);
      await writable.close();
      // Keep the handle so the project can live-sync to this file (auto-save + watch).
      await app.stencilSync.link(handle, handle.name || filename);
    } else {
      svc.downloadBlob(new Blob([text], { type: 'application/x-stencil' }), filename);
    }
    notify('Project saved', 'ok');
  } catch (err) {
    if (err && err.name === 'AbortError') return;   // user cancelled the picker — not an error
    notify('Could not save project: ' + (err.message || err), 'fail');
  }
};

// Open a .stencil project from a File (file input / drag-drop) or raw JSON text. Validates,
// then hands off to DrawingApp.applyProjectFile (which loads it as a fresh local project).
export const openProjectFile = async (svc, input, { from = null } = {}) => {
  const app = svc.app;
  let text;
  try { text = typeof input === 'string' ? input : await input.text(); }
  catch { notify('Could not read project file', 'fail'); return; }
  const res = parseProjectFile(text);
  if (!res.ok) { notify('Invalid .stencil file: ' + res.error, 'fail'); return; }
  try {
    const name = await app.applyProjectFile(res.project);
    // Dropped in: the canvas flies out of the drop point (a project opened from the
    // picker has no point and gets the plain landing).
    arriveFrom(document.getElementById('canvas-container'), from);
    notify(`Opened project “${shortName(name)}”`, 'ok');
  } catch (err) {
    notify('Could not open project: ' + (err.message || err), 'fail');
  }
};

// Prompt for a .stencil file (FS Access open picker when available, else a transient <input>).
export const pickAndOpenProjectFile = async (svc) => {
  if (window.showOpenFilePicker) {
    try {
      const [handle] = await window.showOpenFilePicker({
        types: [{ description: 'Stencil project', accept: { 'application/x-stencil': ['.stencil'] } }],
        multiple: false,
      });
      const file = await handle.getFile();
      await openProjectFile(svc, file);
      // Keep the handle so this project can live-sync to the file it was opened from.
      await svc.app.stencilSync.link(handle, file.name);
    } catch (err) {
      if (err && err.name === 'AbortError') return;
      notify('Could not open project: ' + (err.message || err), 'fail');
    }
    return;
  }
  const inp = document.createElement('input');
  inp.type = 'file';
  inp.accept = '.stencil,application/x-stencil';
  inp.onchange = () => { const f = inp.files && inp.files[0]; if (f) openProjectFile(svc, f); };
  inp.click();
};

// Delete the linked .stencil file from disk (Chromium FileSystemHandle.remove()) after a confirm,
// then drop the link so live-sync stops. Needs a retained handle, so only a file-linked project
// (saved/opened via the picker) can — the project stays open; only the on-disk file is removed.
export const deleteProjectFile = async (svc) => {
  const app = svc.app;
  const sync = app.stencilSync;
  if (!sync.linked) { notify('No linked .stencil file to delete', 'fail'); return; }
  const handle = sync.handle;
  if (typeof handle.remove !== 'function') {
    notify('Deleting files needs a newer Chromium browser', 'fail');
    return;
  }
  const name = sync.name || 'this project file';
  if (!(await app.confirm(
    `Delete “${name}” from disk? This can’t be undone. The project stays open here.`,
    { title: 'Delete project file', confirmLabel: 'Delete file', confirmIcon: 'trash', cancelLabel: 'Cancel' }))) {
    notify('Delete canceled', 'info');
    return;
  }
  try {
    await handle.remove();
    sync.unlink();               // stop auto-save/watch — there's no file to sync to anymore
    notify(`Deleted “${shortName(name)}”`, 'ok');
  } catch (err) {
    if (err && err.name === 'AbortError') return;   // some impls surface a cancelled perm prompt as AbortError
    notify('Could not delete file: ' + (err.message || err), 'fail');
  }
};

