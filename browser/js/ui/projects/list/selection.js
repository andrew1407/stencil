import { setSelectAllFace } from '../../icons.js';
import { revealControls, revealBar } from '../../motion.js';

// The list's multi-select: the checked set, the select-all pool and the batch bar that flies
// with them. `selected` is key -> {kind:'local'|'remote', id, serverUrl, isServer, meta};
// `doomed` is the keys whose removal dust is still playing — on screen, but gone to the bar.
export function createProjectSelection({ batchBar, batchCount, hasServers }) {
  const selected = new Map();
  const doomed = new Set();
  const anyLiveSelectable = () => {
    for (const k of selectables.keys()) if (!doomed.has(k)) return true;
    return false;
  };
  // Retire a key for its whole scatter; returns the undo, called once the dust has landed.
  const retireKey = (key) => {
    doomed.add(key);
    selected.delete(key);
    updateBatchBar();
    // Only after the SETTLE render: the row stays in `selectables` for the whole scatter,
    // so an earlier release flashes Select all back on for the rest of it.
    return () => { doomed.delete(key); updateBatchBar(); };
  };
  const localKey = (id) => `local:${id}`;
  const remoteKey = (m) => `remote:${m.serverUrl}:${m.id}`;
  const isServerMeta = (m) => !!(m && m.remoteId && m.address);
  const sel = () => Array.from(selected.values());
  // Batch eligibility: to-server wants pure-local rows, to-local pure-remote ones.
  const onlyLocalMovable = () => selected.size > 0 && sel().every(s => s.kind === 'local' && !s.isServer);
  const onlyRemoteMovable = () => selected.size > 0 && sel().every(s => s.kind === 'remote');

  const batchBtns = {
    moveServer: document.getElementById('projects-batch-move-server'),
    copyServer: document.getElementById('projects-batch-copy-server'),
    moveLocal: document.getElementById('projects-batch-move-local'),
    copyLocal: document.getElementById('projects-batch-copy-local'),
    remove: document.getElementById('projects-batch-remove'),
    clear: document.getElementById('projects-batch-clear'),
  };
  const selectAllBtn = () => document.getElementById('projects-select-all');
  const selectedGroup = document.getElementById('projects-batch-selected');
  // The controls fly on the app's control clock (REVEAL_GROUP_OUT_MS), never the rows' 220ms
  // box collapse — but they SET OFF with the rows: a removal re-asks in the same turn.
  const updateBatchBar = () => {
    // The bar shows whenever the list HAS selectable rows; it closes only once the controls have
    // FLOWN, since its slot clips them (desktop twin: ProjectsDialog::updateBatchBar).
    const live = anyLiveSelectable();
    revealBar(batchBar, () => selected.size > 0 || anyLiveSelectable());
    // The count rides the buttons' swap; a display flip on the row's FIRST item shoves
    // everything after it sideways in one frame.
    batchCount.textContent = `${selected.size} selected`;
    const local = onlyLocalMovable();
    const remote = onlyRemoteMovable();
    // Inapplicable directions are hidden, not greyed — a disabled button is just noise.
    // A plain display flip inside the group: the GROUP flies, so these carry no cloud.
    const show = (btn, on) => { btn.style.display = on ? '' : 'none'; };
    show(batchBtns.moveServer, local && hasServers());
    show(batchBtns.copyServer, local && hasServers());
    show(batchBtns.moveLocal, remote);
    show(batchBtns.copyLocal, remote);
    // The GROUP comes and goes, not each button: revealControls photographs a control where
    // it sits, and siblings revealed in the same turn are still moving.
    revealControls(batchCount, selected.size > 0);
    revealControls(selectedGroup, selected.size > 0);
    revealControls(selectAllBtn(), live);
    updateSelectAll();
  };
  const clearSelection = () => { selected.clear(); updateBatchBar(); };
  // What THIS render offered a checkbox for (the filtered view) — the select-all pool.
  const selectables = new Map();
  const allSelected = () => {
    let live = 0;
    for (const k of selectables.keys()) {
      if (doomed.has(k)) continue;
      if (!selected.has(k)) return false;
      live++;
    }
    return live > 0;
  };
  // Its label only; the button's coming and going rides updateBatchBar's ordered pass.
  const updateSelectAll = () => setSelectAllFace(selectAllBtn(), allSelected());
  const toggleSelect = (key, entry, on) => {
    if (on) selected.set(key, entry);
    else selected.delete(key);
    updateBatchBar();
  };

  return {
    selected, doomed, selectables, batchBtns, sel, retireKey, localKey, remoteKey,
    isServerMeta, anyLiveSelectable, allSelected, updateBatchBar, updateSelectAll,
    clearSelection, toggleSelect,
  };
}
