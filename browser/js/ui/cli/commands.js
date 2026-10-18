// The CLI stage's own words over the shared table: leaving, the local project listings, the
// new-tab hand-off and the secrets list — plus the skin words, which answer only with a slash.
import { projectsRows } from '../../console/shell/format.js';
import { LIST_WORD, SKIN_NAMES, traitsOf, skinOf } from './skin/table.js';

export const cliCommands = ({ app, stage }) => ({
  clioff() { stage.leave(); },
  exit() { stage.leave(); },
  ls() { return projectsRows(app.storage.store.list(), [], { activeId: app.activeProjectId }); },
  ll() { return projectsRows(app.storage.store.list(), [], { long: true, activeId: app.activeProjectId }); },
  'browser-tab'() {
    if (app.activeProjectId == null) throw new Error('no saved project to open — save it first');
    app.projectTransfer.openProjectInNewTab(app.activeProjectId);
    return 'opened in a new tab';
  },
  clear() { stage.clear(); },
});

// `/eastereggs` lists the words in gold; a skin word puts its skin on, or off when it is on.
export const secretWord = (word, stage) => {
  const w = String(word ?? '').trim().toLowerCase();
  if (w === LIST_WORD) { stage.printSecret(SKIN_NAMES.map((n) => `/${traitsOf(n).word}`)); return true; }
  const skin = skinOf(w);
  if (!skin) return false;
  stage.wearSkin(stage.skin() === skin ? null : skin);
  return true;
};
