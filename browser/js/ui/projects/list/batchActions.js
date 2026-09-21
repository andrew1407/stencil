import { notify, shortName } from '../../../utils.js';
import { leaveThenRemove, rowLeaveDust, ITEM_DUST_MS } from '../../motion.js';
import { deleteRemoteProject as removeRemoteProject } from '../../../net/remoteSync.js';

// The batch-select toolbar's actions, each through runBatch, which names partial failures —
// a failed row comes back on the settle render and would otherwise look silently dropped.
export function wireBatchActions(deps) {
  const {
    app, batchBtns, sel, selected, selectables, doomed, clearSelection, allSelected,
    updateBatchBar, render, pickServer, beginRemoval, rowById, invalidateRemotes,
  } = deps;
  const deleteRemoteProject = (serverUrl, id) => removeRemoteProject(app.connections, serverUrl, id);

  // `rows` overrides the checked set for a caller that has already let it go: the removal clears
  // the selection as the rows start leaving, so what to act on is captured before that.
  const runBatch = async (fn, okMsg, failMsg, settle = null, rows = null) => {
    let done = 0;
    const failures = [];
    for (const s of (rows || sel())) {
      try { await fn(s); done++; }
      catch (err) { failures.push({ name: shortName(s.meta?.name || 'Untitled'), message: err.message }); }
    }
    clearSelection();
    // `settle` is the hold the CALLER opened before the rows started leaving (it knows
    // when that was); batches with no removal just re-render.
    if (settle) await settle(); else render();
    if (!failures.length) { if (done) notify(`${okMsg} (${done})`, 'ok'); return; }
    const names = failures.slice(0, 3).map((f) => `"${f.name}"`).join(', ')
      + (failures.length > 3 ? ` +${failures.length - 3} more` : '');
    notify(done
      ? `${okMsg} ${done} of ${done + failures.length} — failed on ${names}: ${failures[0].message}`
      : `${failMsg} ${names} — ${failures[0].message}`, 'fail');
  };
  batchBtns.clear.addEventListener('click', () => { clearSelection(); render(); });
  // Select-all toggles over the CURRENT render's rows (the filtered view), so a
  // filtered "select all" never sweeps up projects the user cannot see.
  document.getElementById('projects-select-all')?.addEventListener('click', () => {
    if (allSelected()) selected.clear();
    else for (const [k, e] of selectables) selected.set(k, e);
    updateBatchBar();
    render();
  });
  batchBtns.remove.addEventListener('click', async () => {
    if (!selected.size) return;
    if (!(await app.confirm(`Remove ${selected.size} selected project(s)? Server projects are deleted from the server.`, { title: 'Remove projects', danger: true, confirmIcon: 'trash' }))) return;
    // Every selected row scatters at once, then the batch runs. Budgeted: rowLeaveDust coarsens
    // each row's grain on a mass removal so the TOTAL mote count stays bounded.
    const settle = beginRemoval();
    const keys = [...selected.keys()];
    for (const k of keys) doomed.add(k);
    const rows = sel();
    const leaving = Promise.all(rows.map((s, i) =>
      leaveThenRemove(rowById(s.id), () => {}, rowLeaveDust(rows.length, i, ITEM_DUST_MS))));
    // The bar answers NOW, beside the rows' own dust, rather than after it (connections modal
    // parity — the rows are already `doomed`, so nothing is left to select).
    selected.clear();
    updateBatchBar();
    await leaving;
    await runBatch(async (s) => {
      if (s.kind === 'remote') {
        await deleteRemoteProject(s.serverUrl, s.id);
        invalidateRemotes();
      } else { app.removeProject(s.id); }
    }, 'Removed', 'Could not remove', settle, rows);
    // …released only now: runBatch's settle render has rebuilt the pool without them.
    for (const k of keys) doomed.delete(k);
    updateBatchBar();
  });
  batchBtns.moveServer.addEventListener('click', async () => {
    const address = await pickServer('Move the selected projects to which server?');
    if (!address) return;
    await runBatch(s => app.moveProjectToServer(s.id, address), 'Moved to server', 'Could not move');
  });
  batchBtns.copyServer.addEventListener('click', async () => {
    const address = await pickServer('Copy the selected projects to which server?');
    if (!address) return;
    await runBatch(s => app.copyProjectToServer(s.id, address), 'Copied to server', 'Could not copy');
  });
  batchBtns.moveLocal.addEventListener('click', async () => {
    if (!selected.size) return;
    if (!(await app.confirm(`Move ${selected.size} server project(s) to local? They will be removed from the server.`, { title: 'Move to local', confirmLabel: 'Move', confirmIcon: 'download' }))) return;
    await runBatch(s => app.moveProjectToLocal(s.meta), 'Moved to local', 'Could not move');
  });
  batchBtns.copyLocal.addEventListener('click', async () => {
    await runBatch(s => app.copyServerProjectToLocal(s.meta), 'Copied to local', 'Could not copy');
  });
}
