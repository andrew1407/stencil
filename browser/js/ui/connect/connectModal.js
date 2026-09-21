import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import { notify } from '../../utils.js';
import { icon, setSelectAllFace } from '../icons.js';
import { connectModalInner } from './connectMarkup.js';
import { createConnectRowDrag } from './connectRowDrag.js';
import { getAutoConnect, setAutoConnect, getSyncToServer, setSyncToServer } from '../../net/connectionStore.js';
import { isExpiredSession } from '../../net/connectionManager.js';
import { normalizeUrl, isInsecureRemote } from '../../net/connectionManager.js';
import { leaveThenRemove, materialize, createListHold, emptyStateVisible,
  createFilterAnimator, revealControls, revealBar, CONN_DUST_MS, rowDustGrid, rowLeaveDust,
  wipeDurationMs } from '../motion.js';
import { canRefreshList } from '../../core/project/projectOpenGesture.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';

// all | admin | non-admin; an admin credential is one that can mint session tokens.
export const matchesConnFilter = (conn, mode) => {
  if (mode === 'admin') return conn?.credentialKind === 'admin';
  if (mode === 'non-admin') return conn?.credentialKind !== 'admin';
  return true;
};

// One server is named outright, several are counted (desktop parity).
export const batchNote = (verb, urls) =>
  (urls.length === 1 ? `${verb} to ${urls[0]}` : `${verb} ${urls.length} servers`);

// The server connections modal, backed by app.connections (connectionManager).
export class StencilConnectModal extends StencilElement {
  static inner() { return connectModalInner(); }
  static template() { return hostTag('stencil-connect-modal', 'id="connect-modal-overlay" class="app-modal-overlay"', StencilConnectModal.inner()); }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const overlay = $('connect-modal-overlay');
    const urlEl = $('connect-url');
    const tokenEl = $('connect-token');
    const addBtn = $('connect-add');
    const reconnectBtn = $('connect-reconnect');
    const list = $('connect-list');
    // Read lazily: the connection manager is created by createStencil() after stencil:ready.
    const mgr = () => app.connections;

    const selected = new Set();
    const batchBar = $('connect-batch-bar');
    const batchCount = $('connect-batch-count');
    const batchBtns = {
      reconnect: $('connect-batch-reconnect'),
      disconnect: $('connect-batch-disconnect'),
    };
    const selectAllBtn = $('connect-select-all');
    const selectedGroup = $('connect-batch-selected');

    const filterEl = $('connect-filter');
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
    const hold = createListHold({
      settle: () => { render(); list.style.minHeight = ''; },
      wait: () => wipeDurationMs(CONN_DUST_MS),
    });
    // Call before a removal, await the result after: the list sizes the modal, so its
    // height is pinned through the wipe.
    const beginRemoval = () => {
      const held = list.getBoundingClientRect().height;
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

    // Reorder by dropping on a row, disconnect by dropping outside (ui/connectRowDrag.js).
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
        const row = document.createElement('div');
        row.className = 'connect-row';
        row.dataset.url = url;
        const grip = document.createElement('span');
        grip.className = 'connect-grip';
        grip.dataset.title = 'Drag to reorder · drag out of the modal to disconnect';
        grip.textContent = '⋮⋮';
        drag.attach(row, url);
        row.appendChild(grip);
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.className = 'connect-select';
        cb.checked = selected.has(url);
        cb.dataset.title = 'Select for batch action';
        cb.addEventListener('change', () => {
          if (cb.checked) selected.add(url); else selected.delete(url);
          row.classList.toggle('connect-selected', cb.checked);
          updateBatchBar();
        });
        if (cb.checked) row.classList.add('connect-selected');
        const conn = cm.get(url);
        const status = conn ? (conn.status || 'connected') : 'error';
        const statusText = { connected: 'Connected', connecting: 'Connecting…', error: 'Disconnected — not reachable',
          disconnected: 'Disconnected', expired: 'Session expired — reconnect to sign in again' }[status] || status;
        const expired = status === 'expired';
        if (expired) row.classList.add('connect-expired');
        const isAdmin = conn?.credentialKind === 'admin';
        if (isAdmin) row.classList.add('connect-admin');
        const label = document.createElement('span');
        label.className = 'connect-url';
        label.dataset.title = `${statusText} — ${url}`;
        label.innerHTML = `<span class="conn-status conn-status-${status}" data-title="${statusText}"></span>${icon('server', { size: 14 })}<span>${url}</span>`;
        // Its own row child: .connect-url ellipsises a long URL and would clip the badge.
        let badge = null;
        if (isAdmin) {
          badge = document.createElement('span');
          badge.className = 'connect-admin-badge';
          badge.dataset.title = 'Admin credential — this connection can mint session tokens (invite links)';
          badge.innerHTML = `${icon('lock', { size: 12 })}<span>Admin</span>`;
        }
        // The same icon-only button in every state (desktop Connect parity). On an expired
        // session it first asks for a fresh session, then for a token (the admin token works too).
        const recon = document.createElement('button');
        recon.className = 'connect-reconnect-one btn-icon';
        recon.dataset.title = expired ? 'Sign in to this server again' : 'Reconnect this server';
        recon.innerHTML = icon('refresh', { size: 15 });
        recon.addEventListener('click', async () => {
          recon.disabled = true;
          try {
            await mgr().reconnectOne(url, expired ? '' : undefined);
            notify(`Reconnected to ${url}`, 'ok');
          } catch (err) {
            if (!isExpiredSession(err)) notify(`Reconnect failed — ${err.message}`, 'fail');
            else {
              const token = await app.prompt(
                `${url} refused the saved session. Paste an access token — or the server's `
                + 'admin token, which mints a fresh session for you.',
                { title: 'Session expired', confirmLabel: 'Reconnect', confirmIcon: 'link',
                  // Reconnect needs a token: without one the answer was ignored.
                  validate: (v) => (v ? '' : 'Paste a token to reconnect') });
              if (token) {
                try { await mgr().reconnectOne(url, String(token).trim()); notify(`Reconnected to ${url}`, 'ok'); }
                catch (e2) { notify(`Reconnect failed — ${e2.message}`, 'fail'); }
              }
            }
          } finally { recon.disabled = false; }
          render();
        });
        const disc = document.createElement('button');
        disc.className = 'connect-disconnect danger btn-icon';
        disc.dataset.title = 'Disconnect (and forget) this server';
        disc.innerHTML = icon('trash', { size: 15 });
        disc.addEventListener('click', () => confirmDisconnect(url));
        // Their own flex box, so the row's space-between cannot fling them apart.
        const actions = document.createElement('div');
        actions.className = 'connect-actions';
        // Invite: mint a session token and copy `<url>#token=…`; only an admin credential can mint.
        if (conn && conn.connected && conn.credentialKind === 'admin') {
          const invite = document.createElement('button');
          invite.className = 'connect-invite btn-icon';
          invite.dataset.title = 'Copy an invite link (mints a fresh session token)';
          invite.innerHTML = icon('link', { size: 15 });
          invite.addEventListener('click', async () => {
            invite.disabled = true;
            try {
              const link = await conn.mintInvite();
              await navigator.clipboard.writeText(link);
              notify('Invite link copied', 'ok');
            } catch (err) {
              notify(`Invite failed — ${err.message}`, 'fail');
            } finally { invite.disabled = false; }
          });
          actions.append(invite);
        }
        actions.append(recon, disc);
        row.append(cb, label, ...(badge ? [badge] : []), actions);
        list.appendChild(row);
      }
      updateBatchBar();
    };

    const runConnBatch = async (fn, okMsg, failMsg, targets = null) => {
      const ok = [];
      for (const url of (targets || [...selected])) {
        try { await fn(url); ok.push(url); } catch (err) { notify(`${failMsg} ${url} — ${err.message}`, 'fail'); }
      }
      // A caller that named its targets owns the redraw: the disconnect batch has rows in the air.
      if (!targets) { selected.clear(); render(); }
      if (ok.length && okMsg) notify(batchNote(okMsg, ok), 'ok');
    };
    selectAllBtn?.addEventListener('click', () => {
      if (allSelected()) selected.clear();
      else for (const u of shownUrls) if (!doomed.has(u)) selected.add(u);
      render();
    });
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

    const connect = async () => {
      const url = urlEl.value.trim();
      if (!url) { notify('Enter a server URL', 'fail'); return; }
      const token = tokenEl.value.trim();
      addBtn.disabled = true;
      try {
        await mgr().connect(token ? { url, token } : url);
        urlEl.value = '';
        tokenEl.value = '';
        notify('Connected', 'ok');
        if (isInsecureRemote(normalizeUrl(url)))
          notify('Insecure connection: plaintext http — your access token and images are sent unencrypted. Use https on untrusted networks.', 'fail');
        // The new row materializes on the same hold as a removal, so the connections-changed
        // echo cannot rebuild the list mid-animation.
        const settle = hold.begin();
        render();
        materialize(list.querySelector(`[data-url="${CSS.escape(normalizeUrl(url))}"]`),
          rowDustGrid());
        await settle();
      } catch (err) {
        notify(`Could not connect — ${err.message}`, 'fail');
        // A refused credential still leaves a row (connectionManager keeps it in `_expired`)
        // and it gets the same arrival; an unreachable server leaves no row.
        let norm = '';
        try { norm = normalizeUrl(url); } catch { norm = ''; }
        if (norm && mgr().isExpired(norm)) {
          // The URL is in the list now, so leaving it typed in invites adding it twice.
          urlEl.value = '';
          tokenEl.value = '';
          const settle = hold.begin();
          render();
          materialize(list.querySelector(`[data-url="${CSS.escape(norm)}"]`), rowDustGrid());
          await settle();
        }
      } finally {
        addBtn.disabled = false;
      }
    };

    addBtn.addEventListener('click', connect);
    urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); connect(); } });
    tokenEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); connect(); } });
    reconnectBtn.addEventListener('click', async () => {
      try { const urls = mgr().urls; await mgr().reconnect(); notify(batchNote('Reconnected', urls), 'ok'); }
      catch (err) { notify(`Reconnect failed — ${err.message}`, 'fail'); }
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

    const autoEl = $('connect-autoconnect');
    autoEl.checked = getAutoConnect();
    autoEl.addEventListener('change', () => setAutoConnect(autoEl.checked));
    const syncEl = $('connect-sync');
    syncEl.checked = getSyncToServer();
    syncEl.addEventListener('change', () => setSyncToServer(syncEl.checked));

    wireModalShell(overlay, $('connect-btn'), $('connect-close'), {
      // A selection is a transient of one visit, as in the projects modal.
      onOpen: () => { autoEl.checked = getAutoConnect(); syncEl.checked = getSyncToServer(); selected.clear(); render(); },
      // Closing mid-animation finalizes every pending wipe, so a half-removed row cannot reappear.
      onClose: () => hold.finalizeAll(), focusOnOpen: urlEl,
    });

    // A refused saved session must be visible without opening this modal: the Servers
    // button's tooltip says so, mirrored onto the fullscreen toolbar clone. No badge.
    const syncExpiredBadge = () => {
      const on = (mgr()?.expiredUrls?.length || 0) > 0;
      for (const el of [$('connect-btn'), ...document.querySelectorAll('#fs-controls-panel #connect-btn')]) {
        if (el) {
          el.dataset.title = on
            ? 'Servers — a saved session expired, reconnect to sign in again'
            : 'Servers — connect to share & co-edit projects';
        }
      }
    };

    syncExpiredBadge();
    subscribe(EVENTS.connectionsChanged, () => {
      syncExpiredBadge();
      // A live event must not re-render mid-drag or mid-wipe (canRefreshList).
      if (canRefreshList({
        open: overlay.classList.contains('modal-open'),
        dragging: drag.isDragging(),
        removing: hold.holding,
      })) render();
    });
  }
}
define('stencil-connect-modal', StencilConnectModal);
