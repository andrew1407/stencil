import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon, setSelectAllFace } from './icons.js';
import { getAutoConnect, setAutoConnect, getSyncToServer, setSyncToServer } from '../net/connectionStore.js';
import { isExpiredSession } from '../net/connectionManager.js';
import { normalizeUrl, isInsecureRemote } from '../net/connectionManager.js';
import { setTranslucentDragImage } from './dragGhost.js';
import { makeTouchDraggable } from './touchDrag.js';
import { leaveThenRemove, scatterGridFor, materialize, createListHold, emptyStateVisible,
  createFilterAnimator, revealControls } from './motion.js';
import { canRefreshList } from './projectsModal.js';

// Three-way credential filter over the connections list: all | admin | non-admin.
// An ADMIN connection is one whose stored credential can mint session tokens.
export const matchesConnFilter = (conn, mode) => {
  if (mode === 'admin') return conn?.credentialKind === 'admin';
  if (mode === 'non-admin') return conn?.credentialKind !== 'admin';
  return true;
};

// ── Component: server connections modal ─────────────────────────
// Connect to / list / disconnect Stencil servers (URL + optional token); their shared
// projects then appear in the Projects modal. Backed by app.connections (see connectionManager).
export class StencilConnectModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('server', { size: 18 })} Servers</h2>
                <button class="app-modal-close btn-icon-text" id="connect-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Connect a server</div>
                <div class="vs-row vs-field"><label data-title="Server URL, e.g. http://localhost:8090">URL</label>
                    <input type="text" id="connect-url" placeholder="http://localhost:8090">
                </div>
                <div class="vs-row vs-field"><label data-title="Optional access token (issued otherwise)">Token</label>
                    <input type="text" id="connect-token" placeholder="(optional)">
                </div>
                <div class="vs-row vs-actions">
                    <button id="connect-add" class="btn-icon-text" data-title="Connect to the server at the URL above">${icon('plus-circle', { size: 14 })}<span>Connect</span></button>
                    <button id="connect-reconnect" class="btn-icon-text" data-title="Re-establish every connection">${icon('refresh', { size: 15 })}<span>Reconnect all</span></button>
                </div>
                <div class="vs-row vs-checks">
                    <label class="vs-inline-check" data-title="Reconnect saved servers automatically when the editor opens">
                        <input type="checkbox" id="connect-autoconnect"> Auto-connect on open
                    </label>
                    <label class="vs-inline-check" data-title="When off, edits to a fetched server project stay in this session only — never pushed to the server or saved locally (download or 'Make local copy' to keep them)">
                        <input type="checkbox" id="connect-sync"> Sync changes to server
                    </label>
                </div>

                <!-- Heading + credential filter share one row; the filter is view state
                     only, never persisted. -->
                <div class="connect-section-row">
                    <div class="vs-section">Connections</div>
                    <select id="connect-filter" class="modal-filter" data-title="Filter connections by credential">
                        <option value="all">All</option>
                        <option value="admin">Admin</option>
                        <option value="non-admin">Non-admin</option>
                    </select>
                </div>
                <!-- Batch-select toolbar: the projects bar's shape — it stays while the list
                     has rows (it hosts Select all), and only the count + the selection
                     actions come and go with the checked set (updateBatchBar). -->
                <div class="connect-batch-bar" id="connect-batch-bar" style="display:none">
                    <span class="connect-batch-count" id="connect-batch-count" style="display:none">0 selected</span>
                    <span class="connect-batch-actions">
                        <!-- Select all ↔ Deselect all: the one toggle is also the bar's "clear"
                             (a separate Clear did the same thing as Deselect all). -->
                        <button id="connect-select-all" class="btn-icon-text" style="display:none" data-title="Select every listed connection (the current filter's rows)">${icon('check', { size: 13 })}<span>Select all</span></button>
                        <!-- The selection-only actions come and go as ONE group, so the swap is a
                             single flight instead of a button-by-button scramble. -->
                        <span class="connect-batch-selected" id="connect-batch-selected" style="display:none">
                        <button id="connect-batch-reconnect" class="btn-icon-text" data-title="Reconnect the selected servers">${icon('refresh', { size: 13 })}<span>Reconnect</span></button>
                        <button id="connect-batch-disconnect" class="danger btn-icon-text" data-title="Disconnect (and forget) the selected servers">${icon('trash', { size: 13 })}<span>Disconnect</span></button>
                        </span>
                    </span>
                </div>
                <div id="connect-list"><!-- filled by JS --></div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Connections are saved and (optionally) restored on open · server projects show a golden outline.</span>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-connect-modal', 'id="connect-modal-overlay" class="app-modal-overlay"', StencilConnectModal.inner()); }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const overlay = $('connect-modal-overlay');
    const urlEl = $('connect-url');
    const tokenEl = $('connect-token');
    const addBtn = $('connect-add');
    const reconnectBtn = $('connect-reconnect');
    const list = $('connect-list');
    // Read app.connections lazily on each use: the connection manager is created by
    // createStencil() AFTER `stencil:ready` wires this modal, so capturing it once here
    // would pin `undefined` and break Connect forever.
    const mgr = () => app.connections;

    // ── Multi-select state ──
    const selected = new Set();   // urls checked for a batch action
    const batchBar = $('connect-batch-bar');
    const batchCount = $('connect-batch-count');
    const batchBtns = {
      reconnect: $('connect-batch-reconnect'),
      disconnect: $('connect-batch-disconnect'),
    };
    const selectAllBtn = $('connect-select-all');
    const selectedGroup = $('connect-batch-selected');

    // ── Credential filter (view state only — deliberately NOT persisted) ──
    const filterEl = $('connect-filter');
    let filterMode = 'all';
    let shownUrls = new Set();   // urls the last render actually listed

    // Select-all works over the CURRENT render's rows (the filtered view), so a
    // filtered "select all" never sweeps up connections the user cannot see.
    const allSelected = () => shownUrls.size > 0 && [...shownUrls].every((u) => selected.has(u));
    const updateSelectAll = () => setSelectAllFace(selectAllBtn, allSelected());
    const updateBatchBar = () => {
      // The projects selection bar's exact shape (projectsModal.js updateBatchBar): the
      // bar stays put while the list has rows (it hosts Select all) and only the count and
      // the selection actions come and go, as the app's control swap (motion.js
      // revealControls). A bar that never moves has no list jumping beneath it.
      batchBar.style.display = (selected.size || shownUrls.size) ? '' : 'none';
      batchCount.textContent = `${selected.size} selected`;
      revealControls(batchCount, selected.size > 0);
      // ONE flight for the group, not one per button: a control's dust is photographed
      // where it sits, and siblings revealed in the same turn are still sliding.
      revealControls(selectedGroup, selected.size > 0);
      revealControls(selectAllBtn, shownUrls.size > 0);
      updateSelectAll();
    };

    // ── Drag-reorder / drag-out-to-remove state ──
    // draggingUrl: the row being dragged; didReorder: an in-list drop already reordered
    // (dragend must not also drag-out remove); dragActive guards live re-renders mid-drag.
    let draggingUrl = null;
    let didReorder = false;
    let dragActive = false;

    // Wipe hold + refresh gate — the projects modal's beginRemoval pattern via the
    // shared createListHold: while a leave/materialize plays, the connections-changed
    // render is deferred (canRefreshList) and the empty state held back
    // (emptyStateVisible); a close mid-animation finalizes every hold (onClose).
    const hold = createListHold({ settle: () => { render(); list.style.minHeight = ''; } });
    // Call BEFORE a removal, await the result after: this list sizes the modal (so its
    // height is pinned through the wipe) and the empty state must not land under the ash.
    const beginRemoval = () => {
      const held = list.getBoundingClientRect().height;
      if (held) list.style.minHeight = `${held}px`;
      return hold.begin();
    };

    const confirmDisconnect = async (url) => {
      if (!(await app.confirm(`Disconnect and forget ${url}?`, { title: 'Disconnect server', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) { render(); return; }
      // The row scatters before the list is rebuilt without it.
      const settle = beginRemoval();
      await leaveThenRemove(list.querySelector(`[data-url="${CSS.escape(url)}"]`), () => {}, scatterGridFor(1));
      mgr().disconnect(url);
      notify('Disconnected', 'ok');
      await settle();
    };

    // Build the new url order for dropping draggingUrl relative to targetUrl (before/after).
    const orderForDrop = (targetUrl, before) => {
      const cur = mgr().urls.filter((u) => u !== draggingUrl);
      let idx = cur.indexOf(targetUrl);
      if (idx < 0) idx = cur.length - 1;
      cur.splice(before ? idx : idx + 1, 0, draggingUrl);
      return cur;
    };
    const clearDropCues = () => list.querySelectorAll('.connect-drop-before,.connect-drop-after')
      .forEach((el) => el.classList.remove('connect-drop-before', 'connect-drop-after'));

    const render = () => {
      list.innerHTML = '';
      const cm = mgr();
      // Expired sessions keep their row: the server is up, the saved URL is still right,
      // only a new token is missing. Dropping them left the boot 401 with nowhere to go.
      const known = cm ? cm.knownUrls : [];
      // Nothing to re-establish → the button would only toast an error.
      reconnectBtn.disabled = !cm?.reconnectable;
      // Drop any selected urls that are no longer connected (e.g. removed elsewhere).
      // Against the KNOWN set, not the filtered one — filtering a row out of view
      // must not silently drop it from a pending batch action.
      for (const u of [...selected]) if (!known.includes(u)) selected.delete(u);
      const urls = known.filter((u) => matchesConnFilter(cm?.get(u), filterMode));
      shownUrls = new Set(urls);
      if (!urls.length) {
        // Mid-wipe the list stays visually empty (its height still pinned): the
        // placeholder waits for the hold's settle render, or it would land beneath
        // the still-falling dust and read as appearing before the removal finished.
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
        // Drag grip: the row is draggable to REORDER (drop on another row) or to REMOVE
        // (drop outside the modal, with the same confirm as the trash button). Drags starting on an
        // input/button are suppressed so checkbox/action clicks aren't hijacked.
        const grip = document.createElement('span');
        grip.className = 'connect-grip';
        grip.dataset.title = 'Drag to reorder · drag out of the modal to disconnect';
        grip.textContent = '⋮⋮';
        row.draggable = true;
        row.addEventListener('dragstart', (e) => {
          if (e.target.closest('input,button')) { e.preventDefault(); return; }
          draggingUrl = url; didReorder = false; dragActive = true;
          row.classList.add('connect-dragging');
          setTranslucentDragImage(e, row);  // translucent cursor-following ghost
          // Mark this as an internal reorder drag (NOT the url in text/plain — that popped the
          // image-drop overlay + tried to fetch the server URL as an image on drop).
          try { e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('application/x-stencil-reorder', 'connection'); } catch { /* older DnD */ }
        });
        row.addEventListener('dragover', (e) => {
          if (!draggingUrl || draggingUrl === url) return;
          e.preventDefault();
          try { e.dataTransfer.dropEffect = 'move'; } catch { /* noop */ }
          const r = row.getBoundingClientRect();
          const before = e.clientY < r.top + r.height / 2;
          clearDropCues();
          row.classList.add(before ? 'connect-drop-before' : 'connect-drop-after');
        });
        row.addEventListener('dragleave', () => row.classList.remove('connect-drop-before', 'connect-drop-after'));
        row.addEventListener('drop', (e) => {
          if (!draggingUrl || draggingUrl === url) return;
          e.preventDefault();
          e.stopPropagation();
          const r = row.getBoundingClientRect();
          const before = e.clientY < r.top + r.height / 2;
          mgr().reorder(orderForDrop(url, before));
          didReorder = true;
        });
        row.addEventListener('dragend', async (e) => {
          const dragged = draggingUrl;
          draggingUrl = null; dragActive = false;
          row.classList.remove('connect-dragging');
          clearDropCues();
          if (didReorder) { didReorder = false; render(); return; }
          // No in-list drop happened → if released outside the modal card, remove (confirm).
          const card = overlay.querySelector('.app-modal');
          const box = card && card.getBoundingClientRect();
          const outside = box && (e.clientX < box.left || e.clientX > box.right || e.clientY < box.top || e.clientY > box.bottom);
          if (outside && dragged) await confirmDisconnect(dragged);
          else render();
        });
        // Touch/pen: mirror the mouse reorder + drag-out-to-disconnect via the pointer engine.
        makeTouchDraggable(row, {
          canStart: (e) => !e.target.closest('input,button'),
          onStart: () => { draggingUrl = url; didReorder = false; dragActive = true; row.classList.add('connect-dragging'); },
          onMove: (x, y) => {
            clearDropCues();
            const target = document.elementFromPoint(x, y)?.closest('.connect-row');
            if (target && target.dataset.url && target.dataset.url !== draggingUrl) {
              const r = target.getBoundingClientRect();
              target.classList.add(y < r.top + r.height / 2 ? 'connect-drop-before' : 'connect-drop-after');
            }
          },
          onDrop: async (x, y) => {
            const dragged = draggingUrl;
            const target = document.elementFromPoint(x, y)?.closest('.connect-row');
            clearDropCues();
            row.classList.remove('connect-dragging');
            if (target && target.dataset.url && target.dataset.url !== dragged) {
              const r = target.getBoundingClientRect();
              const order = orderForDrop(target.dataset.url, y < r.top + r.height / 2);  // reads draggingUrl (still set)
              draggingUrl = null; dragActive = false;
              mgr().reorder(order);
              render();
              return;
            }
            draggingUrl = null; dragActive = false;
            const card = overlay.querySelector('.app-modal');
            const box = card && card.getBoundingClientRect();
            const outside = box && (x < box.left || x > box.right || y < box.top || y > box.bottom);
            if (outside && dragged) await confirmDisconnect(dragged);
            else render();
          },
          onCancel: () => { draggingUrl = null; dragActive = false; row.classList.remove('connect-dragging'); clearDropCues(); render(); },
        });
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
        // Connection-status dot: green=connected, yellow=connecting/refreshing, red=error/dropped.
        const status = conn ? (conn.status || 'connected') : 'error';
        const statusText = { connected: 'Connected', connecting: 'Connecting…', error: 'Disconnected — not reachable',
          disconnected: 'Disconnected', expired: 'Session expired — reconnect to sign in again' }[status] || status;
        const expired = status === 'expired';
        if (expired) row.classList.add('connect-expired');
        // An ADMIN credential gets the golden outline + badge server projects wear.
        const isAdmin = conn?.credentialKind === 'admin';
        if (isAdmin) row.classList.add('connect-admin');
        const label = document.createElement('span');
        label.className = 'connect-url';
        label.dataset.title = `${statusText} — ${url}`;
        label.innerHTML = `<span class="conn-status conn-status-${status}" data-title="${statusText}"></span>${icon('server', { size: 14 })}<span>${url}</span>`;
        // Its own row child, not inside .connect-url — that box ellipsises a long URL
        // and would clip the badge away with it.
        let badge = null;
        if (isAdmin) {
          badge = document.createElement('span');
          badge.className = 'connect-admin-badge';
          badge.dataset.title = 'Admin credential — this connection can mint session tokens (invite links)';
          badge.innerHTML = `${icon('lock', { size: 12 })}<span>Admin</span>`;
        }
        // Per-row reconnect: the same icon-only button in every state (desktop Connect
        // parity — an expired row says so with the amber fill and the tooltip, not a
        // word the other rows don't carry). On an EXPIRED session it first asks the
        // server for a fresh session, and only if refused asks for a token, which may
        // equally be the ADMIN token.
        const recon = document.createElement('button');
        recon.className = 'connect-reconnect-one btn-icon';
        recon.dataset.title = expired ? 'Sign in to this server again' : 'Reconnect this server';
        recon.innerHTML = icon('refresh', { size: 15 });
        recon.addEventListener('click', async () => {
          recon.disabled = true;
          try {
            await mgr().reconnectOne(url, expired ? '' : undefined);
            notify('Reconnected', 'ok');
          } catch (err) {
            if (!isExpiredSession(err)) notify(`Reconnect failed — ${err.message}`, 'fail');
            else {
              const token = await app.prompt(
                `${url} refused the saved session. Paste an access token — or the server's `
                + 'admin token, which mints a fresh session for you.',
                { title: 'Session expired', confirmLabel: 'Reconnect', confirmIcon: 'link' });
              if (token) {
                try { await mgr().reconnectOne(url, String(token).trim()); notify('Reconnected', 'ok'); }
                catch (e2) { notify(`Reconnect failed — ${e2.message}`, 'fail'); }
              }
            }
          } finally { recon.disabled = false; }
          render();
        });
        const disc = document.createElement('button');
        disc.className = 'connect-disconnect danger btn-icon';
        // Trash, not ✕ — the projects modal's remove glyph; this forgets the server.
        disc.dataset.title = 'Disconnect (and forget) this server';
        disc.innerHTML = icon('trash', { size: 15 });
        disc.addEventListener('click', () => confirmDisconnect(url));
        // Keep reconnect + disconnect grouped tight on the right (their own flex
        // box), rather than letting the row's space-between fling them apart.
        const actions = document.createElement('div');
        actions.className = 'connect-actions';
        // Invite: mint a fresh session token from this row's credential and copy
        // `<url>#token=…` — paste it into any surface's Connect form to join.
        // Only an ADMIN credential can mint: a session-token credential would 401.
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

    // ── Batch actions over the checked connections ──
    const runConnBatch = async (fn, okMsg, failMsg) => {
      let done = 0;
      for (const url of [...selected]) {
        try { await fn(url); done++; } catch (err) { notify(`${failMsg} ${url} — ${err.message}`, 'fail'); }
      }
      selected.clear();
      render();
      if (done) notify(`${okMsg} (${done})`, 'ok');
    };
    selectAllBtn?.addEventListener('click', () => {
      if (allSelected()) selected.clear();
      else for (const u of shownUrls) selected.add(u);
      render();   // re-syncs every row's checkbox; ends in updateBatchBar
    });
    batchBtns.reconnect.addEventListener('click', () => runConnBatch(u => mgr().reconnectOne(u), 'Reconnected', 'Reconnect failed'));
    batchBtns.disconnect.addEventListener('click', async () => {
      if (!selected.size) return;
      if (!(await app.confirm(`Disconnect and forget ${selected.size} selected server(s)?`, { title: 'Disconnect servers', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) return;
      // Every selected row scatters at once, then the batch runs — budgeted like the
      // projects batch (scatterGridFor).
      const settle = beginRemoval();
      await Promise.all([...selected].map((u, i) => leaveThenRemove(
        list.querySelector(`[data-url="${CSS.escape(u)}"]`), () => {}, scatterGridFor(selected.size, i))));
      await runConnBatch(async (u) => mgr().disconnect(u), 'Disconnected', 'Disconnect failed');
      await settle();
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
        // The new row materializes — the removal played backwards (its box expands
        // while a dust copy gathers into it). On the same hold as a removal, so the
        // connections-changed echo can't rebuild the list mid-animation.
        const settle = hold.begin();
        render();
        materialize(list.querySelector(`[data-url="${CSS.escape(normalizeUrl(url))}"]`), scatterGridFor(1));
        await settle();
      } catch (err) {
        notify(`Could not connect — ${err.message}`, 'fail');
      } finally {
        addBtn.disabled = false;
      }
    };

    addBtn.addEventListener('click', connect);
    urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); connect(); } });
    tokenEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); connect(); } });
    reconnectBtn.addEventListener('click', async () => {
      try { await mgr().reconnect(); notify('Reconnected', 'ok'); }
      catch (err) { notify(`Reconnect failed — ${err.message}`, 'fail'); }
      render();
    });

    // Switching the filter re-lists in place, symmetrically: rows the filter DROPS
    // collapse out, then the rebuild, then the rows it REVEALS fade in. Deliberately
    // the light filter effect, never the disconnect's scatter — nothing was removed.
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
      // A selection is a transient of one visit: reopening starts unchecked, exactly as
      // the projects modal does (clearSelection on its onOpen) — user decision.
      onOpen: () => { autoEl.checked = getAutoConnect(); syncEl.checked = getSyncToServer(); selected.clear(); render(); },
      // Closing mid-animation finalizes every pending wipe NOW (render + height
      // release), so a half-removed row can't reappear when the modal next opens.
      onClose: () => hold.finalizeAll(),
    });

    // A refused saved session must be visible WITHOUT opening this modal (the boot
    // toast is transient): the Servers button's TOOLTIP says so while any saved
    // connection needs signing in again — mirrored onto the fullscreen toolbar clone,
    // which is the same button. No badge on the icon: the desktop wears none either.
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
    // Keep the list live when connections change from the console facade or events.
    window.addEventListener('stencil:connections-changed', () => {
      syncExpiredBadge();
      // Guard against a live event re-rendering the list mid-drag (destroying the
      // dragged element) or mid-wipe (projectsModal's canRefreshList gate; the hold's
      // settle render catches up).
      if (canRefreshList({
        open: overlay.classList.contains('modal-open'),
        dragging: dragActive,
        removing: hold.holding,
      })) render();
    });
  }
}
define('stencil-connect-modal', StencilConnectModal);
