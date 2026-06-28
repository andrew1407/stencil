import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { getAutoConnect, setAutoConnect, getSyncToServer, setSyncToServer } from '../net/connectionStore.js';

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
                <div class="vs-row"><label title="Server URL, e.g. http://host:8090">URL</label>
                    <input type="text" id="connect-url" placeholder="http://host:8090">
                </div>
                <div class="vs-row"><label title="Optional access token (issued otherwise)">Token</label>
                    <input type="text" id="connect-token" placeholder="(optional)">
                </div>
                <div class="vs-row">
                    <button id="connect-add" class="btn-icon-text">${icon('plus-circle', { size: 14 })}<span>Connect</span></button>
                    <button id="connect-reconnect" class="btn-icon-text" title="Re-establish every connection">${icon('refresh', { size: 15 })}<span>Reconnect all</span></button>
                </div>
                <div class="vs-row">
                    <label title="Reconnect saved servers automatically when the editor opens">
                        <input type="checkbox" id="connect-autoconnect"> Auto-connect on open
                    </label>
                </div>
                <div class="vs-row">
                    <label title="When off, edits to a fetched server project stay in this session only — never pushed to the server or saved locally (download or 'Make local copy' to keep them)">
                        <input type="checkbox" id="connect-sync"> Sync changes to server
                    </label>
                </div>

                <div class="vs-section">Connections</div>
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

    const render = () => {
      list.innerHTML = '';
      const cm = mgr();
      const urls = cm ? cm.urls : [];
      if (!urls.length) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = 'No servers connected.';
        list.appendChild(empty);
        return;
      }
      for (const url of urls) {
        const row = document.createElement('div');
        row.className = 'connect-row';
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
        disc.addEventListener('click', () => {
          mgr().disconnect(url);
          notify('Disconnected', 'ok');
          render();
        });
        // Keep reconnect + disconnect grouped tight on the right (their own flex
        // box), rather than letting the row's space-between fling them apart.
        const actions = document.createElement('div');
        actions.className = 'connect-actions';
        actions.append(recon, disc);
        row.append(label, actions);
        list.appendChild(row);
      }
    };

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
      if (overlay.classList.contains('modal-open')) render();
    });
  }
}
define('stencil-connect-modal', StencilConnectModal);
