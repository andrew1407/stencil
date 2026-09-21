// The .stencil project file: save, open, pick and delete. File System Access pickers where
// they exist (keeping the handle for live sync), else a download / transient <input>.
import { notify, shortName } from '../../utils.js';
import { arriveFrom } from '../../ui/motion.js';
import { serializeProjectFile, parseProjectFile } from './projectFile.js';

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
      // The handle lets the project live-sync to this file (auto-save + watch).
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

// From a File (input / drag-drop) or raw JSON text.
export const openProjectFile = async (svc, input, { from = null } = {}) => {
  const app = svc.app;
  let text;
  try { text = typeof input === 'string' ? input : await input.text(); }
  catch { notify('Could not read project file', 'fail'); return; }
  const res = parseProjectFile(text);
  if (!res.ok) { notify('Invalid .stencil file: ' + res.error, 'fail'); return; }
  try {
    const name = await app.applyProjectFile(res.project);
    // A project from the picker has no drop point and gets the plain landing.
    arriveFrom(document.getElementById('canvas-container'), from);
    notify(`Opened project “${shortName(name)}”`, 'ok');
  } catch (err) {
    notify('Could not open project: ' + (err.message || err), 'fail');
  }
};

export const pickAndOpenProjectFile = async (svc) => {
  if (window.showOpenFilePicker) {
    try {
      const [handle] = await window.showOpenFilePicker({
        types: [{ description: 'Stencil project', accept: { 'application/x-stencil': ['.stencil'] } }],
        multiple: false,
      });
      const file = await handle.getFile();
      await openProjectFile(svc, file);
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

// Chromium FileSystemHandle.remove() after a confirm, then unlink so live-sync stops; the
// project stays open.
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
    sync.unlink();
    notify(`Deleted “${shortName(name)}”`, 'ok');
  } catch (err) {
    if (err && err.name === 'AbortError') return;   // some impls surface a cancelled perm prompt as AbortError
    notify('Could not delete file: ' + (err.message || err), 'fail');
  }
};

