import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { getAutoConnect, setAutoConnect, getSyncToServer, setSyncToServer } from '../net/connectionStore.js';
import { normalizeUrl, isInsecureRemote } from '../net/connectionManager.js';
import { setTranslucentDragImage } from './dragGhost.js';
import { makeTouchDraggable } from './touchDrag.js';

// ── Component: server connections modal ─────────────────────────
// Connect to / list / disconnect Stencil servers (URL + optional token); their shared
// projects then appear in the Projects modal. Backed by app.connections (see connectionManager).
export class StencilConnectModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('server', { size: 18 })} Servers</h2>
                <button class="app-modal-close btn-icon-text" id="connect-close" title="Close (Esc)">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Connect a server</div>
                <div class="vs-row vs-field"><label title="Server URL, e.g. http://host:8090">URL</label>
                    <input type="text" id="connect-url" placeholder="http://host:8090">
                </div>
                <div class="vs-row vs-field"><label title="Optional access token (issued otherwise)">Token</label>
                    <input type="text" id="connect-token" placeholder="(optional)">
                </div>
                <div class="vs-row">
                    <button id="connect-add" class="btn-icon-text" title="Connect to the server at the URL above">${icon('plus-circle', { size: 14 })}<span>Connect</span></button>
                    <button id="connect-reconnect" class="btn-icon-text" title="Re-establish every connection">${icon('refresh', { size: 15 })}<span>Reconnect all</span></button>
                </div>
                <div class="vs-row vs-checks">
                    <label class="vs-inline-check" title="Reconnect saved servers automatically when the editor opens">
                        <input type="checkbox" id="connect-autoconnect"> Auto-connect on open
                    </label>
                    <label class="vs-inline-check" title="When off, edits to a fetched server project stay in this session only — never pushed to the server or saved locally (download or 'Make local copy' to keep them)">
                        <input type="checkbox" id="connect-sync"> Sync changes to server
                    </label>
                </div>

                <div class="vs-section">Connections</div>
                <!-- Batch-select toolbar: appears once one or more connections are checked. -->
                <div class="connect-batch-bar" id="connect-batch-bar" style="display:none">
                    <span class="connect-batch-count" id="connect-batch-count">0 selected</span>
                    <span class="connect-batch-actions">
                        <button id="connect-batch-reconnect" class="btn-icon-text" title="Reconnect the selected servers">${icon('refresh', { size: 13 })}<span>Reconnect</span></button>
                        <button id="connect-batch-disconnect" class="danger btn-icon-text" title="Disconnect (and forget) the selected servers">${icon('x', { size: 13 })}<span>Disconnect</span></button>
                        <button id="connect-batch-clear" class="btn-icon-text" title="Clear selection">${icon('x', { size: 13 })}<span>Clear</span></button>
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
      clear: $('connect-batch-clear'),
    };
    const updateBatchBar = () => {
      batchBar.style.display = selected.size ? '' : 'none';
      batchCount.textContent = `${selected.size} selected`;
    };

    // ── Drag-reorder / drag-out-to-remove state ──
    // draggingUrl: the row being dragged; didReorder: a valid in-list drop already reordered
    // (so dragend doesn't also treat it as a drag-out remove); dragActive guards the live
    // stencil:connections-changed re-render from destroying the row mid-drag.
    let draggingUrl = null;
    let didReorder = false;
    let dragActive = false;

    // Yes/No confirm then disconnect — the single remove path shared by the ✕ button and
    // the drag-out gesture (mirrors projectsModal's danger-confirm idiom exactly).
    const confirmDisconnect = async (url) => {
      if (!(await app.confirm(`Disconnect and forget ${url}?`, { title: 'Disconnect server', danger: true, confirmLabel: 'Yes', cancelLabel: 'No' }))) { render(); return; }
      mgr().disconnect(url);
      notify('Disconnected', 'ok');
      render();
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
      const urls = cm ? cm.urls : [];
      // Drop any selected urls that are no longer connected (e.g. removed elsewhere).
      for (const u of [...selected]) if (!urls.includes(u)) selected.delete(u);
      if (!urls.length) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = 'No servers connected.';
        list.appendChild(empty);
        updateBatchBar();
        return;
      }
      for (const url of urls) {
        const row = document.createElement('div');
        row.className = 'connect-row';
        row.dataset.url = url;
        // Drag grip: the row is draggable to REORDER (drop on another row) or to REMOVE
        // (drop outside the modal, with the same confirm as the ✕). Drags starting on an
        // input/button are suppressed so checkbox/action clicks aren't hijacked.
        const grip = document.createElement('span');
        grip.className = 'connect-grip';
        grip.title = 'Drag to reorder · drag out of the modal to disconnect';
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
        // Multi-select checkbox for batch reconnect/disconnect.
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.className = 'connect-select';
        cb.checked = selected.has(url);
        cb.title = 'Select for batch action';
        cb.addEventListener('change', () => {
          if (cb.checked) selected.add(url); else selected.delete(url);
          row.classList.toggle('connect-selected', cb.checked);
          updateBatchBar();
        });
        if (cb.checked) row.classList.add('connect-selected');
        const conn = cm.get(url);
        // Connection-status dot: green=connected, yellow=connecting/refreshing, red=error/dropped.
        const status = conn ? (conn.status || 'connected') : 'error';
        const statusText = { connected: 'Connected', connecting: 'Connecting…', error: 'Disconnected — not reachable', disconnected: 'Disconnected' }[status] || status;
        const label = document.createElement('span');
        label.className = 'connect-url';
        label.title = `${statusText} — ${url}`;
        label.innerHTML = `<span class="conn-status conn-status-${status}" title="${statusText}"></span>${icon('server', { size: 14 })}<span>${url}</span>`;
        // Per-row reconnect — re-establish just this server (token re-validated).
        const recon = document.createElement('button');
        recon.className = 'connect-reconnect-one btn-icon';
        recon.title = 'Reconnect this server';
        recon.innerHTML = icon('refresh', { size: 15 });
        recon.addEventListener('click', async () => {
          recon.disabled = true;
          try { await mgr().reconnectOne(url); notify('Reconnected', 'ok'); }
          catch (err) { notify(`Reconnect failed — ${err.message}`, 'fail'); }
          finally { recon.disabled = false; }
          render();
        });
        const disc = document.createElement('button');
        disc.className = 'connect-disconnect danger btn-icon';
        disc.title = 'Disconnect';
        disc.innerHTML = icon('x', { size: 15 });
        disc.addEventListener('click', () => confirmDisconnect(url));
        // Keep reconnect + disconnect grouped tight on the right (their own flex
        // box), rather than letting the row's space-between fling them apart.
        const actions = document.createElement('div');
        actions.className = 'connect-actions';
        actions.append(recon, disc);
        row.append(cb, label, actions);
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
    batchBtns.clear.addEventListener('click', () => { selected.clear(); render(); });
    batchBtns.reconnect.addEventListener('click', () => runConnBatch(u => mgr().reconnectOne(u), 'Reconnected', 'Reconnect failed'));
    batchBtns.disconnect.addEventListener('click', async () => {
      if (!selected.size) return;
      if (!(await app.confirm(`Disconnect and forget ${selected.size} selected server(s)?`, { title: 'Disconnect servers', danger: true, confirmLabel: 'Yes', cancelLabel: 'No' }))) return;
      runConnBatch(async (u) => mgr().disconnect(u), 'Disconnected', 'Disconnect failed');
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
        render();
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

    // Auto-connect-on-open toggle: reflect the saved preference and persist changes.
    const autoEl = $('connect-autoconnect');
    autoEl.checked = getAutoConnect();
    autoEl.addEventListener('change', () => setAutoConnect(autoEl.checked));
    const syncEl = $('connect-sync');
    syncEl.checked = getSyncToServer();
    syncEl.addEventListener('change', () => setSyncToServer(syncEl.checked));

    wireModalShell(overlay, $('connect-btn'), $('connect-close'), { onOpen: () => { autoEl.checked = getAutoConnect(); syncEl.checked = getSyncToServer(); render(); } });

    // Keep the list live when connections change from the console facade or events.
    window.addEventListener('stencil:connections-changed', () => {
      // Guard against a live status/reorder event re-rendering the list mid-drag, which
      // would destroy the element the browser is dragging.
      if (!dragActive && overlay.classList.contains('modal-open')) render();
    });
  }
}
define('stencil-connect-modal', StencilConnectModal);
