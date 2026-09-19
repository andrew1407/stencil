// ── §10 project adapters: resolve by name, then the modal's own guarded flows ──
// Each returns a NOTE string when nothing happened (unknown / ambiguous / declined),
// so a refused plan reads as words rather than failing.
import { toHexColor } from '../../core/accents.js';
import { resolveProjectByName } from '../projectNames.js';
export const projectAdapters = (app) => ({
  removeProjectNamed: async (name) => {
    const { meta, note } = resolveProjectByName(app, name);
    if (note) return note;
    const ok = await app.confirm(`Remove project "${meta.name}"? This cannot be undone.`,
      { title: 'Remove project', danger: true, confirmIcon: 'trash' });
    if (!ok) return 'removal canceled';
    app.removeProject(meta.id);
    return null;
  },
  clearWorkingImage: async () => {
    if (!app.image && !app.lines.length) return 'nothing to remove';
    const what = app.storage.incognito
      ? 'the image in this incognito editor' : 'the unsaved image and its lines';
    const ok = await app.confirm(`Remove ${what}? This cannot be undone.`,
      { title: 'Remove image', danger: true, confirmIcon: 'trash' });
    if (!ok) return 'removal canceled';
    app.newEditor({ keepChat: true });
    return null;
  },
  openProjectNamed: async (name, last = false) => {
    // "The last project I worked on": the store lists newest-edited first, so the head of the
    // list IS the answer — resolved HERE, never by the model (it never sees the project list).
    const recent = last ? app.storage.store.list()[0] : null;
    if (last && !recent) return 'there are no saved projects yet';
    const { meta, note } = last ? { meta: recent } : resolveProjectByName(app, name);
    if (note) return note;
    if (meta.id === app.activeProjectId) return `"${meta.name}" is already open`;
    if (app.storage.temporary && (app.image || app.lines.length)) {
      const ok = await app.confirm(
        `Open "${meta.name}" here? Any unsaved changes in the current tab will be replaced.`,
        { title: 'Open project', confirmLabel: 'Open', confirmIcon: 'folder' });
      if (!ok) return 'open canceled';
    }
    return app.switchToProject(meta.id) ? null : `could not open "${meta.name}"`;
  },
  // §10 renameProject: the inline rename control's path; the store's own
  // duplicate-name refusal surfaces as the note.
  renameActiveProject: async (name) => {
    const id = app.activeProjectId;
    if (id == null) return 'no active saved project to rename';
    if (app.storage.store.nameExists(name, id)) return `a project named "${name}" already exists`;
    return app.renameProject(id, name) ? null : `could not rename to "${name}"`;
  },
  // §10 blankColor: valid only on a BLANK project (keeps the drawn lines).
  // CSS names resolve to hex first (the setters take normalizeHex forms only).
  setBlankColor: async (color) => {
    const hex = toHexColor(color);
    if (app.activeProjectId != null) {
      return app.setProjectBlankColor(app.activeProjectId, hex)
        ? null : 'only a blank project has a recolourable background';
    }
    if (!app.activeIsBlank() || !app.image) return 'only a blank project has a recolourable background';
    app.setBlankColor(hex);
    return null;
  },
  // §10 clearProjects: every saved local project, or every one BUT the open one (`keepCurrent`)
  // — "delete the others" otherwise lost the project when there was nothing to re-save (user report).
  clearLocalProjects: async (keepCurrent = false) => {
    const all = app.storage.store.list();
    const keepId = keepCurrent ? app.activeProjectId : null;
    const doomed = keepId == null ? all : all.filter((m) => m.id !== keepId);
    if (!doomed.length) {
      return all.length ? 'no other saved projects to clear' : 'no saved projects to clear';
    }
    const kept = keepId == null ? null : all.find((m) => m.id === keepId);
    const ok = await app.confirm(kept
      ? `Delete the ${doomed.length} other saved local project${doomed.length === 1 ? '' : 's'}, keeping "${kept.name}"? This cannot be undone.`
      : `Delete every saved local project (${doomed.length})? This cannot be undone.`,
      { title: kept ? 'Clear other projects' : 'Clear all projects', danger: true, confirmIcon: 'trash' });
    if (!ok) return 'clear canceled';
    if (!kept) app.clearAllProjects();
    else for (const m of doomed) app.removeProject(m.id);
    return null;
  },
});
