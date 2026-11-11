// The address bar names the open project (contracts/launch/launch-uri.md §2): `?project=<name>` for a
// saved one, its server link for a server-only one, no `project` once it closes. Incognito, a framed
// editor and a launch not yet applied leave the URL as it is.
import { withOpenProject } from '../../../core/launch/query.js';

const framed = (win) => { try { return win.parent != null && win.parent !== win; } catch { return true; } };

// A saved project by its name — the picture's until the first save names it, as the title reads —
// else a server project by `<server>/projects/<id>`; else none.
export const openProjectRef = (app) => {
  const id = app.activeProjectId;
  const name = id != null ? String(app.storage?.store?.getMeta?.(id)?.name || app.imageBaseName || '').trim() : '';
  if (name) return name;
  const link = app.remoteLink;
  return link?.address && link?.remoteId ? `${String(link.address).replace(/\/+$/, '')}/projects/${link.remoteId}` : null;
};

// True when the URL was rewritten. `launchQuery` is undefined until boot reads the URL, a query until
// its applier starts and null after, `launchApplying` true while it runs; a `#stencil…` fragment is
// a hand-off the launch has yet to read.
export const syncProjectUrl = (app, win = globalThis.window) => {
  const loc = win?.location;
  if (!loc || typeof win.history?.replaceState !== 'function') return false;
  if (app.storage?.incognito || app.launchQuery !== null || app.launchApplying) return false;
  if (framed(win) || /^#stencil[=-]/.test(loc.hash || '')) return false;
  const next = withOpenProject(loc.search, loc.hash, openProjectRef(app));
  if (!next) return false;
  win.history.replaceState(win.history.state, '', loc.pathname + next.search + next.hash);
  return true;
};
