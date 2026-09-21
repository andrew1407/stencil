// ── §2.1 editor adapters: saving the working image, and the composer nudge ──
// A save promotes to a FRESH project id, so a multi-image plan leaves one project
// per image instead of overwriting one.
import { CHAT_ATTACHMENTS_EVENT } from '../chat/controller.js';
import { publish } from '../../eventBus/appBus.js';
import { uniqueProjectName } from '../projectNames.js';
export const editorAdapters = (app) => ({
  // Send drained the queued attachments — repaint every composer's chips.
  onAttachmentsChanged: () => {
    publish(CHAT_ATTACHMENTS_EVENT);
  },
  // §2.1 `save`: persist as a LOCAL project (publishing stays a user action). Each save
  // promotes to a FRESH project id, so a multi-image plan leaves one project per image.
  saveProject: async (name) => {
    if (!app.image) throw new Error('there is no image to save');
    const wanted = String(name || app.imageBaseName || 'Untitled').trim() || 'Untitled';
    const unique = uniqueProjectName(app, wanted);
    app.storage.promoteTemporaryToProject();
    app.imageBaseName = unique;
    app.storage.save();
    app.renameProject(app.activeProjectId, unique);
    app.updateProjectTitle?.();
    return unique;
  },
});
