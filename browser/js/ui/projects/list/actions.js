// The two buttons under the projects list: a fresh editor tab, and Clear All. Clear All wipes
// only LOCAL projects — server ones are never touched — and plays every row out first.
import { notify } from '../../../utils.js';
import { leaveThenRemove, rowLeaveDust, ITEM_DUST_MS } from '../../motion.js';

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
    // The same leave every removal plays, real saved rows only (desktop: scatterRows).
    const rows = [...list.querySelectorAll('.project-row:not(.project-temp)')];
    const settle = beginRemoval();
    // The bar's controls leave with them: every selectable row is going.
    const keys = [...selectables.keys()];
    for (const k of keys) doomed.add(k);
    const leaving = Promise.all(rows.map((row, i) =>
      leaveThenRemove(row, () => {}, rowLeaveDust(rows.length, i, ITEM_DUST_MS))));
    selected.clear();
    updateBatchBar();
    await leaving;
    app.clearAllProjects();
    await settle();
    for (const k of keys) doomed.delete(k);
    updateBatchBar();
  });
}
