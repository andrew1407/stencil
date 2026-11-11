// The two buttons under the projects list: a fresh editor tab, and Clear All. Clear All wipes
// only LOCAL projects — server ones are never touched — and plays the local rows out first.
import { notify } from '../../../utils.js';
import { rowLeave } from './rowMotion.js';
import { rowLeaveDust, ITEM_DUST_MS } from '../../motion/surface/tiles.js';
import { clearedToast } from '../../../core/project/transferController.js';

// What Clear All Local plays out: the local saved rows and their keys; a server row stays put rather than
// scattering and arriving again (user report; desktop: scatterRows over the local keys).
export const localLeavers = (list, keys) => ({
  rows: [...list.querySelectorAll('.project-row:not(.project-temp):not(.project-remote)')],
  keys: [...keys].filter((k) => k.startsWith('local:')),
});

export function wireListActions({ app, els, list, hasServers, selected, selectables, doomed,
  updateBatchBar, beginRemoval, close }) {
  // A fresh editor tab discards nothing, so there is nothing to confirm.
  els.newEditorBtn.addEventListener('click', () => {
    if (!window.open(location.origin + location.pathname, '_blank')) {
      notify('The browser blocked the new tab — allow pop-ups for this page', 'fail');
      return;
    }
    close();
  });
  els.clearAllBtn.addEventListener('click', async () => {
    const msg = hasServers()
      ? 'Are you sure? This permanently deletes ALL local projects. Server projects are not affected.'
      : 'Are you sure? This permanently deletes ALL saved projects.';
    const title = hasServers() ? 'Delete all local projects' : 'Delete all projects';
    if (!(await app.confirm(msg, { title, danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) return;
    const { rows, keys } = localLeavers(list, selectables.keys());
    const settle = beginRemoval();
    for (const k of keys) doomed.add(k);
    const leaving = Promise.all(rows.map((row, i) =>
      rowLeave(row, rowLeaveDust(rows.length, i, ITEM_DUST_MS))));
    for (const k of keys) selected.delete(k);
    updateBatchBar();
    await leaving;
    const cleared = app.storage.store.list().length;
    app.projectTransfer.clearAllProjects();
    await settle();
    for (const k of keys) doomed.delete(k);
    updateBatchBar();
    if (cleared) notify(clearedToast(cleared), 'ok');
  });
}
