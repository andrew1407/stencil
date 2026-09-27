// The Servers batch bar's actions: reconnect or disconnect every selected server, one
// toast for the lot. The selection and the removal hold belong to the list (./list.js).
// Desktop twin: dialogs/connect/ConnectDialogBatchBar.cpp.
import { notify } from '../../utils.js';
import { leaveThenRemove, CONN_DUST_MS, rowLeaveDust } from '../motion.js';
import { batchNote } from './rules.js';

export function wireConnectBatch(app, { list, mgr, batchBtns }, { selected, doomed, render, updateBatchBar, beginRemoval }) {
  const runConnBatch = async (fn, okMsg, failMsg, targets = null) => {
    const ok = [];
    for (const url of (targets || [...selected])) {
      try { await fn(url); ok.push(url); } catch (err) { notify(`${failMsg} ${url} — ${err.message}`, 'fail'); }
    }
    // A caller that named its targets owns the redraw: the disconnect batch has rows in the air.
    if (!targets) { selected.clear(); render(); }
    if (ok.length && okMsg) notify(batchNote(okMsg, ok), 'ok');
  };
  batchBtns.reconnect.addEventListener('click', () => runConnBatch(u => mgr().reconnectOne(u), 'Reconnected', 'Reconnect failed'));
  batchBtns.disconnect.addEventListener('click', async () => {
    if (!selected.size) return;
    if (!(await app.confirm(`Disconnect and forget ${selected.size} selected server(s)?`, { title: 'Disconnect servers', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) return;
    // Every selected row scatters at once, budgeted like the projects batch (scatterGridFor).
    const targets = [...selected];
    const settle = beginRemoval();
    for (const u of targets) doomed.add(u);
    selected.clear();
    const leaving = Promise.all(targets.map((u, i) => leaveThenRemove(
      list.querySelector(`[data-url="${CSS.escape(u)}"]`), () => {},
      rowLeaveDust(targets.length, i, CONN_DUST_MS))));
    updateBatchBar();
    await runConnBatch(async (u) => mgr().disconnect(u), null, 'Disconnect failed', targets);
    await leaving;
    await settle();
    for (const u of targets) doomed.delete(u);
    updateBatchBar();
  });
}
