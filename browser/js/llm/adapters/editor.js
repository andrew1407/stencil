// ── §2.1 editor adapters: saving the working image, and the composer nudge ──
// A save promotes to a FRESH project id, so a multi-image plan leaves one project
// per image instead of overwriting one.
import { CHAT_ATTACHMENTS_EVENT } from '../chat/controller.js';
import { publish } from '../../eventBus/appBus.js';
import { uniqueProjectName } from '../projectNames.js';
import { editorMemento } from '../../core/historyStack.js';
export const editorAdapters = (app) => ({
  onAttachmentsChanged: () => {
    publish(CHAT_ATTACHMENTS_EVENT);
  },
  // §2.1 `save`: persist as a LOCAL project (publishing stays a user action).
  saveProject: async (name) => {
    if (!app.image) throw new Error('there is no image to save');
    const wanted = String(name || app.imageBaseName || 'Untitled').trim() || 'Untitled';
    const unique = uniqueProjectName(app, wanted);
    app.storage.promoteTemporaryToProject();
    app.imageBaseName = unique;
    app.storage.save();
    app.projectTransfer.renameProject(app.activeProjectId, unique);
    app.updateProjectTitle?.();
    return unique;
  },
  // A sandboxed preview's way back: the undo stack, its cursor and floor, and the view on screen.
  editorHistory: {
    mark: () => {
      const h = app.history;
      return { steps: h.history.slice(), step: h.historyStep, floor: h.floor, memento: structuredClone(editorMemento(app)),
        picked: [app.selectedLineIdx, [...(app.selectedLines ?? [])], app.coordLineIdx] };
    },
    rewind: (mark) => {
      Object.assign(app.history, { history: mark.steps.slice(), historyStep: mark.step, floor: mark.floor });
      app.restoreHistoryStep(structuredClone(mark.memento));
      // The marked lines are back, so is what was picked on them, which a preview's install let go.
      [app.selectedLineIdx, app.selectedLines, app.coordLineIdx] = mark.picked;
      if (app.selectedLineIdx >= 0) app.showSelectionPanel(app.lines[app.selectedLineIdx]);
      app.renderer.redraw();
      app.updateButtons();
      app.coordTable.update();
      app.storage.saveSoon();
      app.remoteSync.scheduleRemoteSync();
    },
  },
});
