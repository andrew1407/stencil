import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import INFO from '../../config/infoConfig.json' with { type: 'json' };
import { icon } from '../icons.js';
import { isKeyCombo, keysHtml, escapeHtml } from '../tip/tipContent.js';
export class StencilInfoModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('help', { size: 18 })} Controls &amp; Shortcuts Info</h2>
                <button class="app-modal-close btn-icon-text" id="info-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div style="padding:12px 18px 6px;">
                <input type="text" id="info-search" placeholder="Search controls…" style="width:100%;padding:8px 10px;border:1px solid var(--border-main);border-radius:6px;background:var(--input-bg);color:var(--input-text);font-size:13px;">
            </div>
            <div class="settings-body" id="info-body"><!-- filled by JS --></div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-info-modal', 'id="info-modal-overlay" class="app-modal-overlay"', StencilInfoModal.inner()); }

  wire(_app) {
    const overlay = document.getElementById('info-modal-overlay');
    const openBtn = document.getElementById('info-btn');
    const closeBtn = document.getElementById('info-close');
    const search = document.getElementById('info-search');
    const body = document.getElementById('info-body');

// Every key-combo token becomes a keycap, the rest stays prose (mirrored by the desktop info dialog).
    const keyTermHtml = term => String(term).replace(/\s*\+\s*/g, '+')
      .split(/(\s+|[()\/])/)
      .map(tok => (tok && isKeyCombo(tok) ? keysHtml(tok) : escapeHtml(tok)))
      .join('');

    const render = filter => {
      const q = (filter || '').trim().toLowerCase();
      let html = '';
      let anyVisible = false;
      INFO.forEach(([group, items]) => {
        const matches = items.filter(([k, d]) =>
          !q || k.toLowerCase().includes(q) || d.toLowerCase().includes(q));
        if (matches.length === 0) return;
        anyVisible = true;
        html += `<div class="info-group-title">${group}</div>`;
        matches.forEach(([k, d]) => {
          html += `<div class="info-item shimmer"><span class="info-key">${keyTermHtml(k)}</span><span class="info-desc">${escapeHtml(d)}</span></div>`;
        });
      });
      body.innerHTML = anyVisible ? html : '<div class="info-empty">No matching controls.</div>';
    }

    search.addEventListener('input', () => render(search.value));

    wireModalShell(overlay, openBtn, closeBtn, {
      onOpen: () => { search.value = ''; render(''); setTimeout(() => search.focus(), 30); },
    });
  }
}
define('stencil-info-modal', StencilInfoModal);
