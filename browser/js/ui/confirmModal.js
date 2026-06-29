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

    const body = overlay.querySelector('.settings-body');

    // Resolver for the in-flight ask()/choose(); null when no dialog is open.
    let resolveCurrent = null;
    // When set, the dialog is in "choose" mode: Confirm resolves with the picked
    // value, Cancel/Close/Escape resolve null (instead of the plain boolean).
    let choiceSelect = null;
    // When set, the dialog is in "prompt" mode: Confirm resolves the trimmed text.
    let promptInput = null;
    const settle = (val) => {
      overlay.classList.remove('modal-open');
      document.removeEventListener('keydown', onKey, true);
      const r = resolveCurrent; resolveCurrent = null;
      const selEl = choiceSelect; choiceSelect = null;
      const inp = promptInput; promptInput = null;
      if (selEl) selEl.parentElement?.remove();   // drop the injected picker row
      if (inp) inp.parentElement?.remove();       // drop the injected prompt row
      if (!r) return;
      if (selEl) r(val ? selEl.value : null);
      else if (inp) r(val ? inp.value.trim() : null);
      else r(val);
    };
    const onKey = (e) => {
      if (e.key === 'Escape') { e.stopPropagation(); settle(false); }
      else if (e.key === 'Enter') { e.preventDefault(); settle(true); }
    };
    // Shared open: set labels/danger, show the overlay, arm the key handler.
    const beginDialog = (message, opts, defaultTitle) => {
      document.getElementById('confirm-modal-title-text').textContent = opts.title || defaultTitle;
      document.getElementById('confirm-modal-message').textContent = message || '';
      document.getElementById('confirm-modal-confirm-text').textContent = opts.confirmLabel || 'Confirm';
      document.getElementById('confirm-modal-cancel-text').textContent = opts.cancelLabel || 'Cancel';
      confirmBtn.classList.toggle('danger', !!opts.danger);
      overlay.classList.add('modal-open');
      document.addEventListener('keydown', onKey, true);
    };

    closeBtn.addEventListener('click', () => settle(false));
    cancelBtn.addEventListener('click', () => settle(false));
    confirmBtn.addEventListener('click', () => settle(true));
    overlay.addEventListener('mousedown', e => { if (e.target === overlay) settle(false); });

    // Cancel any in-flight dialog before a new one opens, dropping its injected row. A picker/
    // prompt resolves null (its cancel value); a plain ask resolves false.
    const dismissPrevious = () => {
      if (resolveCurrent) {
        const prev = resolveCurrent;
        resolveCurrent = null;
        prev(choiceSelect || promptInput ? null : false);
      }
      if (choiceSelect) { choiceSelect.parentElement?.remove(); choiceSelect = null; }
      if (promptInput) { promptInput.parentElement?.remove(); promptInput = null; }
    };
    // Build a one-element row (select or input) and inject it below the message.
    const injectRow = (el) => {
      const wrap = document.createElement('div');
      wrap.className = 'confirm-choose-row';
      wrap.appendChild(el);
      body.appendChild(wrap);
    };

    // Public API consumed by app.confirm().
    this.ask = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Confirm');
      setTimeout(() => confirmBtn.focus(), 30);
    });

    // Picker variant: same modal with a <select> injected below the message.
    // Resolves the chosen option value on Confirm, null on Cancel/Close/Escape.
    // opts: { title, confirmLabel, cancelLabel, options:[{value,label}] }.
    this.choose = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Choose');
      // The picker row is created here, not in static markup, so the markup tests stay green.
      const sel = document.createElement('select');
      sel.className = 'confirm-choose-select';
      for (const o of (opts.options || [])) {
        const opt = document.createElement('option');
        opt.value = o.value;
        opt.textContent = o.label != null ? o.label : o.value;
        sel.appendChild(opt);
      }
      injectRow(sel);
      choiceSelect = sel;
      setTimeout(() => sel.focus(), 30);
    });

    // Text-prompt variant: a single <input> below the message. Resolves the trimmed text on
    // Confirm, null on Cancel/Close/Escape. opts: { title, confirmLabel, defaultValue }.
    this.prompt = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Enter a name');
      const inp = document.createElement('input');
      inp.type = 'text';
      inp.className = 'confirm-prompt-input';
      inp.value = opts.defaultValue || '';
      inp.addEventListener('keydown', e => e.stopPropagation());   // keep the modal's Enter/Esc, but let typing through
      injectRow(inp);
      promptInput = inp;
      setTimeout(() => { inp.focus(); inp.select(); }, 30);
    });
  }
}
define('stencil-confirm-modal', StencilConfirmModal);
