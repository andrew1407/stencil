import { notify } from '../../../utils.js';
import { wireExportOptionsMenu } from '../../export/exportOptionsMenu.js';
import { wireBlankColorButton } from './blankColorButton.js';
import { wireProjectColorButton } from './projectColorButton.js';
import { wireProjectNameField } from './projectNameField.js';
import { setupHoldZoom } from '../viewport/holdZoom.js';
export function wireToolbarButtons(app) {
  wireProjectNameField(app);
  wireProjectColorButton(app);
  wireBlankColorButton(app);
  // One button, both directions — the same branch the context menu's draw toggle uses.
  document.getElementById('draw-toggle').addEventListener('click', () => {
    if (app.isDrawing) app.stopDrawingMode();
    else app.startDrawingMode();
  });
  document.getElementById('draw-mode-toggle').addEventListener('click', () => {
    app.setDrawMode(app.drawMode === 'rect' ? 'line' : 'rect');
    app.storage.save();
  });
  document.getElementById('undo').addEventListener('click', () => app.undo());
  document.getElementById('redo').addEventListener('click', () => app.redo());
  document.getElementById('download-json').addEventListener('click', () => app.export.downloadJSON());
  document.getElementById('copy-json-btn').addEventListener('click', () => app.export.copyLayoutToClipboard());
  wireExportOptionsMenu(document.getElementById('save-image'), app, {
    run: (variant) => app.export.saveImage(variant),
    currentIcon: 'download',
    hotkeyIds: { current: 'saveImage', original: 'saveImageOriginal', tint: 'saveImageTint' },
  });
  document.getElementById('upload-json-btn').addEventListener('click', () => document.getElementById('upload-json').click());
  document.getElementById('upload-json').addEventListener('change', e => app.export.uploadJSON(e));
  // Shift+click saves without embedding the light/dark + accent theme (a portable, theme-neutral
  // .stencil); a plain click carries it. The hotkey/synthetic-click path has no shiftKey → theme in.
  document.getElementById('save-project-btn').addEventListener('click', e => app.export.saveProjectFile({ includeTheme: !e.shiftKey }));
  document.getElementById('open-project-btn').addEventListener('click', () => app.export.pickAndOpenProjectFile());
  document.getElementById('live-sync-btn').addEventListener('click', () => { app.stencilSync.liveSync = !app.stencilSync.liveSync; });
  document.getElementById('delete-project-btn').addEventListener('click', () => app.export.deleteProjectFile());
  document.getElementById('clear-storage').addEventListener('click', async () => {
    if (app.storage.temporary || app.activeProjectId == null) {
      // Temporary editor → just clear the editor back to blank.
      if (await app.confirm('Clear this editor (image + lines)?', { title: 'Clear editor', danger: true, confirmIcon: 'trash' })) {
        app.storage.newTemporary();
        app.tabs.reportActive(null);
        // The clear worked, so it reads as a success — a red ✕ said the opposite.
        // Wording matches the desktop's two branches (MainWindow.cpp clearProject).
        app.showSaveStatus('Editor cleared', 'var(--success)', 'check');
      }
      return;
    }
    // A server-linked project only clears its LOCAL copy — the server keeps it.
    // Say so up front, and confirm the user really wants to drop the open project.
    const server = app.remoteLink?.address;
    const msg = server
      ? `Remove the local copy of this project? It is stored on the server ${server} and will stay there.`
      : 'Clear this project (image + lines) from storage?';
    if (await app.confirm(msg, { title: server ? 'Remove local copy' : 'Clear project', danger: true, confirmIcon: 'trash' })) {
      app.remoteLink = null;   // dropped the local session → no server link to save back to
      // ONE removal path (drawingApp.removeProject): storage, the stored chat (§12.2),
      // the drop to a blank editor and the cross-tab notify all happen there.
      app.removeProject(app.activeProjectId);
      if (server) notify(`Local copy removed — still on the server ${server}`, 'info');
      app.showSaveStatus('Project cleared', 'var(--success)', 'check');
    }
  });
  const incognitoBtn = document.getElementById('incognito-toggle');
  if (incognitoBtn) incognitoBtn.addEventListener('click', () => {
    if (!app.canToggleIncognito()) return;
    app.storage.incognito = !app.storage.incognito;
    app.updateIncognitoUI();
    notify(app.storage.incognito
      ? 'Incognito mode — this editor won\'t be saved'
      : 'Incognito off', 'info');
  });
  document.getElementById('clear-all-lines').addEventListener('click', () => app.clearAllLines());
  // Zoom buttons: single click = small step, double-click = large step,
  // hold = continuous zoom (kicks in after a short delay)
  setupHoldZoom(app.zoomPan, document.getElementById('zoom-in'), +1);
  setupHoldZoom(app.zoomPan, document.getElementById('zoom-out'), -1);
  document.getElementById('zoom-fit').addEventListener('click', () => app.zoomPan.fitToWindow());
}
