// The Servers list: its rows, the credential filter, the batch selection and its bar, and
// the removal hold that keeps a wipe from being re-rendered under itself.
// Desktop twin: dialogs/connect/ConnectDialog.cpp, ConnectDialogAuth.cpp, ConnectDialogFilter.cpp.
import { setSelectAllFace } from '../icons.js';
import { createConnectRowDrag } from './rowDrag.js';
import { connectRow } from './row.js';
import { matchesConnFilter } from './rules.js';
import { releaseHeldHeight } from '../motion/easeBoxHeight.js';
import { leaveThenRemove, createListHold, emptyStateVisible, createFilterAnimator, revealControls,
  revealBar, CONN_DUST_MS, rowLeaveDust, wipeDurationMs } from '../motion.js';

export function createConnectList(app, {
  overlay, list, reconnectBtn, filterEl, batchBar, batchCount, selectAllBtn, selectedGroup, mgr,
}) {
  const selected = new Set();

  let filterMode = 'all';
  let shownUrls = new Set();
  // …minus rows playing their removal dust: gone as far as the bar is concerned, so Select
  // all leaves with the row. The desktop's `doomed_` (ConnectDialog.cpp).
  const doomed = new Set();
  const anyLiveShown = () => {
    for (const u of shownUrls) if (!doomed.has(u)) return true;
    return false;
  };

  // Select-all works over the current render's rows, never over filtered-out ones.
  const allSelected = () => {
    let live = 0;
    for (const u of shownUrls) {
      if (doomed.has(u)) continue;
      if (!selected.has(u)) return false;
      live++;
    }
    return live > 0;
  };
  const updateSelectAll = () => setSelectAllFace(selectAllBtn, allSelected());
  // The controls fly on the control clock (motion.js REVEAL_GROUP_OUT_MS, the desktop's
  // CONTROL_REVEAL_OUT_MS), never the row's box collapse; they still set off with the row.
  const updateBatchBar = () => {
    // The bar stays while the list has rows and its controls come and go (revealControls); the bar
    // closes only once they have flown (desktop: ConnectDialog::updateBatchBar).
    const live = anyLiveShown();
    revealBar(batchBar, () => selected.size > 0 || anyLiveShown());
    batchCount.textContent = `${selected.size} selected`;
    revealControls(batchCount, selected.size > 0);
    // One flight for the group: siblings revealed in the same turn are still sliding.
    revealControls(selectedGroup, selected.size > 0);
    revealControls(selectAllBtn, live);
    updateSelectAll();
  };

  // The projects modal's beginRemoval pattern via createListHold: mid-wipe the
  // connections-changed render is deferred (canRefreshList) and the empty state held back.
  let held = 0;
  let letGo = () => {};
  const hold = createListHold({
    settle: () => { render(); letGo = releaseHeldHeight(list, held); },
    wait: () => wipeDurationMs(CONN_DUST_MS),
  });
  // Call before a removal, await the result after: the list sizes the modal, so its
  // height is pinned through the wipe.
  const beginRemoval = () => {
    held = list.getBoundingClientRect().height;
    letGo();
    if (held) list.style.minHeight = `${held}px`;
    return hold.begin();
  };

  const confirmDisconnect = async (url) => {
    if (!(await app.confirm(`Disconnect and forget ${url}?`, { title: 'Disconnect server', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) { render(); return; }
    const settle = beginRemoval();
    doomed.add(url);
    selected.delete(url);
    const leaving = leaveThenRemove(list.querySelector(`[data-url="${CSS.escape(url)}"]`),
      () => {}, rowLeaveDust(1, 0, CONN_DUST_MS));
    mgr().disconnect(url);
    // The bar answers now, beside the row's dust: the connections-changed echo is held off mid-wipe.
    updateBatchBar();
    await leaving;
    await settle();
    // Released only after the settle render: the row outlives its own box collapse.
    doomed.delete(url);
    updateBatchBar();
  };

  // Reorder by dropping on a row, disconnect by dropping outside (ui/connect/rowDrag.js).
  const drag = createConnectRowDrag({
    list, overlay, mgr, render: () => render(), confirmDisconnect: (u) => confirmDisconnect(u),
  });

  const render = () => {
    list.innerHTML = '';
    const cm = mgr();
    // Expired sessions keep their row: only a new token is missing.
    const known = cm ? cm.knownUrls : [];
    reconnectBtn.disabled = !cm?.reconnectable;
    // Against the known set, not the filtered one: filtering a row out of view must not
    // drop it from a pending batch action.
    for (const u of [...selected]) if (!known.includes(u)) selected.delete(u);
    const urls = known.filter((u) => matchesConnFilter(cm?.get(u), filterMode));
    shownUrls = new Set(urls);
    if (!urls.length) {
      // Mid-wipe the placeholder waits for the hold's settle render.
      if (emptyStateVisible(urls.length, hold.holding)) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = known.length ? 'No connections match this filter.' : 'No servers connected.';
        list.appendChild(empty);
      }
      updateBatchBar();
      return;
    }
    for (const url of urls) {
      list.appendChild(connectRow(url, cm, { app, mgr, selected, drag, updateBatchBar, render, confirmDisconnect }));
    }
    updateBatchBar();
  };

  selectAllBtn?.addEventListener('click', () => {
    if (allSelected()) selected.clear();
    else for (const u of shownUrls) if (!doomed.has(u)) selected.add(u);
    render();
  });

  // A filter switch re-lists in place with the light filter effect, never the
  // disconnect's scatter.
  const runFilter = createFilterAnimator({
    keys: () => shownUrls,
    next: () => {
      const cm = mgr();
      return (cm ? cm.knownUrls : []).filter((u) => matchesConnFilter(cm?.get(u), filterMode));
    },
    render,
    find: (u) => list.querySelector(`[data-url="${CSS.escape(u)}"]`),
  });
  filterEl.addEventListener('change', () => { filterMode = filterEl.value; runFilter(); });

  return { selected, doomed, hold, drag, render, updateBatchBar, beginRemoval };
}
