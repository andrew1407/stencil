// Closing the project open in this tab while it stays in Projects, with the "Closed …" notice: the
// closeProject hotkey asks first, the projects window's ✕ drop does not, and the console's
// stencil.closeProject() closes without either. Desktop twin: ProjectFlows::closeActiveProject.
import { notify, shortName } from '../../utils.js';

export const closeProjectMessage = (name) =>
  `Close "${shortName(name || 'Untitled')}" in this tab? It stays saved in Projects — nothing is removed.`;

// True once closed; with nothing open a toast says so. `ask: false` closes without the question.
export async function confirmCloseProject(app, { closeAnchor = null, ask = true } = {}) {
  const id = app.activeProjectId;
  if (id == null) {
    notify('No project is open', 'info');
    return false;
  }
  const name = app.storage.store.getMeta(id)?.name;
  const yes = !ask || await app.confirm(closeProjectMessage(name),
    { title: 'Close project?', confirmLabel: 'Close', confirmIcon: 'x', cancelLabel: 'Cancel', closeAnchor });
  // Another project opened while the question was up: that one was not asked about.
  if (!yes || app.activeProjectId !== id) return false;
  app.closeProject(id);
  notify(`Closed "${shortName(name || 'Untitled')}"`, 'ok');
  return true;
}
