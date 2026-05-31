import { StencilElement, hostTag, define } from './base.js';
import INFO from '../config/infoConfig.json' with { type: 'json' };
// ── Component: controls & shortcuts info modal ──────────────────
export class StencilInfoModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>📋 Controls &amp; Shortcuts</h2>
                <button class="app-modal-close" id="infoClose">✕ Close</button>
            </div>
            <div style="padding:12px 18px 6px;">
                <input type="text" id="infoSearch" placeholder="Search controls…" style="width:100%;padding:8px 10px;border:1px solid var(--border-main);border-radius:6px;background:var(--input-bg);color:var(--input-text);font-size:13px;">
            </div>
            <div class="settings-body" id="infoBody"><!-- filled by JS --></div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-info-modal', 'id="infoModalOverlay" class="app-modal-overlay"', StencilInfoModal.inner()); }

  wire(_app) {
    const overlay = document.getElementById('infoModalOverlay');
    const openBtn = document.getElementById('infoBtn');
    const closeBtn = document.getElementById('infoClose');
    const search = document.getElementById('infoSearch');
    const body = document.getElementById('infoBody');

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
          html += `<div class="info-item"><span class="info-key">${k}</span><span class="info-desc">${d}</span></div>`;
        });
      });
      body.innerHTML = anyVisible ? html : '<div class="info-empty">No matching controls.</div>';
    }

    search.addEventListener('input', () => render(search.value));

    const open = ()  => { search.value = ''; render(''); overlay.classList.add('modal-open'); setTimeout(() => search.focus(), 30); };
    const close = () => { overlay.classList.remove('modal-open'); };
    openBtn.addEventListener('click', open);
    closeBtn.addEventListener('click', close);
    overlay.addEventListener('mousedown', e => { if (e.target === overlay) close(); });
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && overlay.classList.contains('modal-open')) close();
    });
  }
}
define('stencil-info-modal', StencilInfoModal);
