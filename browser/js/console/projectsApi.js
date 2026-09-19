// ── window.stencil's project collections ─────────────────────────────────────
// Project handles (project.js) over the local registry plus this tab's incognito
// session. `openedIds` is the tab coordinator's view of what is open elsewhere.
// The expire() help text is DurationParser's grammar (core/parse/DurationParser.cpp).
import { str } from './coerce.js';
import { DURATION_HELP } from './project.js';

export const createProjectsApi = ({ app, makeProject, openedIds }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  const incognitoList = () => (app.storage.incognito ? [makeProject(null, true)] : []);
  const savedOpen = () => {
    const open = openedIds();
    return app.storage.store.list().filter((m) => open.has(m.id)).map((m) => makeProject(m.id));
  };

  const api = {
    // ── Projects ──
    get current() {
      if (app.activeProjectId != null) return makeProject(app.activeProjectId);
      if (app.storage.incognito) return makeProject(null, true);
      return null;
    },
    // Open in some tab/window — INCLUDING this tab's incognito editor (it's open too).
    get openedProjects() { return incognitoList().concat(savedOpen()); },
    get archivedProjects() {
      const open = openedIds();
      return app.storage.store.list().filter((m) => !open.has(m.id)).map((m) => makeProject(m.id));
    },
    // Only the CURRENT tab's incognito editor is knowable (others report null).
    get incognitoProjects() { return incognitoList(); },
    // Default = currently-open saved projects; flags add the archived/incognito sets.
    getProjects({ archived = false, incognito = false } = {}) {
      let list = savedOpen();
      if (archived) list = list.concat(stencil.archivedProjects);
      if (incognito) list = list.concat(incognitoList());
      return list;
    },
    getProjectByName(name) {
      const n = str(name).trim().toLowerCase();
      const m = app.storage.store.list().find((p) => str(p.name).trim().toLowerCase() === n);
      return m ? makeProject(m.id) : null;
    },
    // Local projects whose keywords match ANY of the query terms (case-insensitive substring),
    // mirroring the CLI /keywords-search. Returns Project handles, most-recently-updated first.
    getProjectsByKeyword(...keywords) {
      const terms = keywords.flatMap((k) => (Array.isArray(k) ? k : str(k).split(/[\s,]+/)))
        .map((s) => str(s).trim().toLowerCase()).filter(Boolean);
      if (!terms.length) return [];
      return app.storage.store.list()
        .filter((p) => (p.keywords || []).some((kw) => terms.some((t) => str(kw).toLowerCase().includes(t))))
        .map((p) => makeProject(p.id));
    },
    expire(spec) {
      const s = str(spec).trim();
      if (!s) return DURATION_HELP;
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to set an expiration on — open or create one first');
      return makeProject(id).expire(s);
    },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
