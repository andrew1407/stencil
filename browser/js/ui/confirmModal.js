import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';

// ── Component: generic confirm dialog ───────────────────────────
// A single reusable yes/no modal replacing native confirm(). Call via the
// instance method ask(message, opts) → Promise<boolean>; resolves true on Confirm,
// false on Cancel / Close / overlay-click / Escape. opts: { title, confirmLabel,
// cancelLabel, danger }. app.confirm() (drawingApp) delegates here.
export class StencilConfirmModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal app-modal-confirm">
            <div class="settings-header">
                <h2 id="confirm-modal-title">${icon('alert', { size: 18 })} <span id="confirm-modal-title-text">Confirm</span></h2>
                <button class="app-modal-close btn-icon-text" id="confirm-modal-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <p id="confirm-modal-message" class="confirm-message"></p>
            </div>
            <div class="settings-footer">
                <span class="footer-hint"></span>
                <button id="confirm-modal-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span id="confirm-modal-cancel-text">Cancel</span></button>
                <button id="confirm-modal-confirm" class="btn-icon-text">${icon('check', { size: 14 })}<span id="confirm-modal-confirm-text">Confirm</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-confirm-modal', 'id="confirm-modal-overlay" class="app-modal-overlay"', StencilConfirmModal.inner()); }

  wire() {
    const overlay = document.getElementById('confirm-modal-overlay');
    const closeBtn = document.getElementById('confirm-modal-close');
    const cancelBtn = document.getElementById('confirm-modal-cancel');
    const confirmBtn = document.getElementById('confirm-modal-confirm');

    // Resolver for the in-flight ask(); null when no dialog is open.
    let resolveCurrent = null;
    const settle = (val) => {
      overlay.classList.remove('modal-open');
      document.removeEventListener('keydown', onKey, true);
      const r = resolveCurrent; resolveCurrent = null;
      if (r) r(val);
    };
    const onKey = (e) => {
      if (e.key === 'Escape') { e.stopPropagation(); settle(false); }
      else if (e.key === 'Enter') { e.preventDefault(); settle(true); }
    };

    closeBtn.addEventListener('click', () => settle(false));
    cancelBtn.addEventListener('click', () => settle(false));
    confirmBtn.addEventListener('click', () => settle(true));
    overlay.addEventListener('mousedown', e => { if (e.target === overlay) settle(false); });

    // Public API consumed by app.confirm().
    this.ask = (message, opts = {}) => new Promise(resolve => {
      // A second ask while one is open: cancel the previous.
      if (resolveCurrent) { const prev = resolveCurrent; resolveCurrent = null; prev(false); }
      resolveCurrent = resolve;
      document.getElementById('confirm-modal-title-text').textContent = opts.title || 'Confirm';
      document.getElementById('confirm-modal-message').textContent = message || '';
      document.getElementById('confirm-modal-confirm-text').textContent = opts.confirmLabel || 'Confirm';
      document.getElementById('confirm-modal-cancel-text').textContent = opts.cancelLabel || 'Cancel';
      confirmBtn.classList.toggle('danger', !!opts.danger);
      overlay.classList.add('modal-open');
      document.addEventListener('keydown', onKey, true);
      setTimeout(() => confirmBtn.focus(), 30);
    });
  }
}
define('stencil-confirm-modal', StencilConfirmModal);
