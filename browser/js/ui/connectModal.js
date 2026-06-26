import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';

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
                    <button id="connect-reconnect" class="btn-icon" title="Reconnect all">${icon('refresh', { size: 15 })}</button>
                </div>

                <div class="vs-section">Connections</div>
                <div id="connect-list"><!-- filled by JS --></div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Server projects appear in Projects with a golden outline.</span>
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
    const mgr = app.connections;

    const render = () => {
      list.innerHTML = '';
      const urls = mgr ? mgr.urls : [];
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
        const label = document.createElement('span');
        label.className = 'connect-url';
        label.innerHTML = `${icon('server', { size: 14 })}<span>${url}</span>`;
        const disc = document.createElement('button');
        disc.className = 'connect-disconnect danger btn-icon';
        disc.title = 'Disconnect';
        disc.innerHTML = icon('x', { size: 15 });
        disc.addEventListener('click', () => {
          mgr.disconnect(url);
          notify('Disconnected', 'ok');
          render();
        });
        row.append(label, disc);
        list.appendChild(row);
      }
    };

    const connect = async () => {
      const url = urlEl.value.trim();
      if (!url) { notify('Enter a server URL', 'fail'); return; }
      const token = tokenEl.value.trim();
      addBtn.disabled = true;
      try {
        await mgr.connect(token ? { url, token } : url);
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
      try { await mgr.reconnect(); notify('Reconnected', 'ok'); }
      catch (err) { notify(`Reconnect failed — ${err.message}`, 'fail'); }
      render();
    });

    wireModalShell(overlay, $('connect-btn'), $('connect-close'), { onOpen: render });

    // Keep the list live when connections change from the console facade or events.
    window.addEventListener('stencil:connections-changed', () => {
      if (overlay.classList.contains('modal-open')) render();
    });
  }
}
define('stencil-connect-modal', StencilConnectModal);
