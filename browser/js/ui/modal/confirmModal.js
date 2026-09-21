import { StencilElement, hostTag, define, createModalFlight } from '../base.js';
import { icon } from '../icons.js';
import { gestureAnchorRect } from '../canvas/gesturePoint.js';

// The reusable yes/no modal behind app.confirm(): ask(message, opts) → Promise<boolean>,
// false on Cancel / Close / overlay-click / Escape. `confirmIcon` / `altIcon` name a glyph
// from ui/icons.js so the button's icon says what it does.
export class StencilConfirmModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal app-modal-confirm">
            <div class="settings-header">
                <h2 id="confirm-modal-title"><span id="confirm-modal-title-icon">${icon('alert', { size: 18 })}</span> <span id="confirm-modal-title-text">Confirm</span></h2>
                <button class="app-modal-close btn-icon-text" id="confirm-modal-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <p id="confirm-modal-message" class="confirm-message"></p>
            </div>
            <div class="settings-footer">
                <span class="footer-hint"></span>
                <button id="confirm-modal-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span id="confirm-modal-cancel-text">Cancel</span></button>
                <button id="confirm-modal-confirm" class="btn-icon-text">${icon('check', { size: 14 })}<span id="confirm-modal-confirm-text">OK</span></button>
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
    // No opener icon: the dialog forms out of the gesture that raised it and pours back into it.
    const flight = createModalFlight(overlay, () => overlay.querySelector('.app-modal'));

    // Captured on open and reused on close, or the close would anchor on the dismiss button.
    let openAnchor = null;
    // opts.closeAnchor names another way back (a context-menu row is gone by close time);
    // an element, measured at close time.
    let closeAnchorEl = null;
    const rectOf = (el) => {
      const r = el?.getBoundingClientRect?.();
      return r && r.width > 0 && r.height > 0 ? r : null;
    };
    let resolveCurrent = null;
    // "choose" mode: Confirm resolves the picked value, Cancel/Close/Escape null.
    let choiceSelect = null;
    let promptInput = null;
    // Live validation (desktop twin: modalChrome.hpp PromptSpec::validate): the reason the
    // trimmed text cannot be accepted, '' when it can; a reason disables Confirm and Enter.
    let promptValidate = null;
    let promptReasonEl = null;
    const promptWhyNot = () =>
      (promptInput && promptValidate ? (promptValidate(promptInput.value.trim()) || '') : '');
    const revalidatePrompt = () => {
      if (!promptInput) return;
      const why = promptWhyNot();
      confirmBtn.disabled = !!why;
      if (why) confirmBtn.dataset.title = why; else delete confirmBtn.dataset.title;
      if (promptReasonEl) {
        promptReasonEl.textContent = why;
        promptReasonEl.style.display = why ? '' : 'none';
      }
    };
    const clearPromptGate = () => {
      promptValidate = null;
      promptReasonEl = null;
      confirmBtn.disabled = false;
      delete confirmBtn.dataset.title;
    };
    // A third button (askAlt): Confirm resolves 'confirm', the extra button 'alt', Cancel null.
    let altBtn = null;
    const settle = (val) => {
      // Measured while still up (display:none measures 0); the cloud lives on <body>, so the
      // answer never waits for it.
      const animate = overlay.classList.contains('modal-open') && !flight.reducedMotion()
                      && flight.setOrigin(rectOf(closeAnchorEl) || openAnchor);
      overlay.classList.remove('modal-open');
      if (animate) flight.playClosing();
      else { flight.settle(); flight.finishClose(); }
      document.removeEventListener('keydown', onKey, true);
      const r = resolveCurrent; resolveCurrent = null;
      const selEl = choiceSelect; choiceSelect = null;
      const inp = promptInput; promptInput = null;
      clearPromptGate();
      const alt = altBtn; altBtn = null;
      if (selEl) selEl.parentElement?.remove();   // drop the injected picker row
      if (inp) inp.parentElement?.remove();       // drop the injected prompt row
      if (alt) alt.remove();                      // drop the injected third button
      if (!r) return;
      if (selEl) r(val ? selEl.value : null);
      else if (inp) r(val ? inp.value.trim() : null);
      else if (alt) r(val === 'alt' ? 'alt' : (val ? 'confirm' : null));
      else r(val);
    };
    const onKey = (e) => {
      if (e.key === 'Escape') { e.stopPropagation(); settle(false); }
      else if (e.key === 'Enter') {
        // A multi-line prompt owns plain Enter; only the modifier form confirms there.
        if (promptInput?.tagName === 'TEXTAREA' && e.target === promptInput && !(e.ctrlKey || e.metaKey)) return;
        e.preventDefault();
        if (promptWhyNot()) return;
        settle(true);
      }
    };
    const beginDialog = (message, opts, defaultTitle) => {
      document.getElementById('confirm-modal-title-text').textContent = opts.title || defaultTitle;
      // The alert triangle for a question with a consequence, `titleIcon` for a plain value prompt.
      document.getElementById('confirm-modal-title-icon').innerHTML =
        icon(opts.titleIcon || 'alert', { size: 18 });
      document.getElementById('confirm-modal-message').textContent = message || '';
      // The glyph follows the action: a named action ("Replace") shows what it does.
      confirmBtn.innerHTML =
        icon(opts.confirmIcon || 'check', { size: 14 }) + '<span id="confirm-modal-confirm-text"></span>';
      document.getElementById('confirm-modal-confirm-text').textContent = opts.confirmLabel || 'OK';
      document.getElementById('confirm-modal-cancel-text').textContent = opts.cancelLabel || 'Cancel';
      confirmBtn.classList.toggle('danger', !!opts.danger);
      flight.finishClose();
      overlay.classList.add('modal-open');
      // Measured after the class applies (no size while display:none); reused by settle().
      openAnchor = gestureAnchorRect();
      closeAnchorEl = opts.closeAnchor || null;
      if (!flight.reducedMotion() && flight.setOrigin(openAnchor)) flight.playDust(true);
      document.addEventListener('keydown', onKey, true);
    };

    closeBtn.addEventListener('click', () => settle(false));
    cancelBtn.addEventListener('click', () => settle(false));
    confirmBtn.addEventListener('click', () => settle(true));
    overlay.addEventListener('mousedown', e => { if (e.target === overlay) settle(false); });

    // A picker/prompt resolves null (its cancel value); a plain ask resolves false.
    const dismissPrevious = () => {
      if (resolveCurrent) {
        const prev = resolveCurrent;
        resolveCurrent = null;
        prev(choiceSelect || promptInput || altBtn ? null : false);
      }
      if (choiceSelect) { choiceSelect.parentElement?.remove(); choiceSelect = null; }
      if (promptInput) { promptInput.parentElement?.remove(); promptInput = null; }
      clearPromptGate();
      if (altBtn) { altBtn.remove(); altBtn = null; }
    };
    const injectRow = (el) => {
      const wrap = document.createElement('div');
      wrap.className = 'confirm-choose-row';
      wrap.appendChild(el);
      body.appendChild(wrap);
    };

    this.ask = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Confirm');
      setTimeout(() => confirmBtn.focus(), 30);
    });

    // Cancel | <altLabel> | <confirmLabel>, resolving 'confirm', 'alt' or null.
    // opts: { title, confirmLabel, altLabel, cancelLabel, confirmIcon, altIcon, danger }.
    this.askAlt = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Confirm');
      // Injected at runtime so the static markup (and the markup tests) stay unchanged.
      const btn = document.createElement('button');
      btn.id = 'confirm-modal-alt';
      btn.className = 'btn-icon-text';
      // A glyph like the other two, or it reads as the odd one out.
      btn.innerHTML = icon(opts.altIcon || 'plus', { size: 14 }) + '<span></span>';
      btn.querySelector('span').textContent = opts.altLabel || 'Alternative';
      btn.addEventListener('click', () => settle('alt'));
      cancelBtn.parentElement.insertBefore(btn, cancelBtn.nextSibling);
      altBtn = btn;
      setTimeout(() => confirmBtn.focus(), 30);
    });

    // A <select> below the message: resolves the chosen value, null on Cancel/Close/Escape.
    // opts: { title, confirmLabel, cancelLabel, options:[{value,label}] }.
    this.choose = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Choose');
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

    this.prompt = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Enter a name');
      const multiline = !!opts.multiline;
      const inp = document.createElement(multiline ? 'textarea' : 'input');
      if (multiline) inp.rows = opts.rows || 3;
      else inp.type = 'text';
      inp.className = 'confirm-prompt-input';
      inp.value = opts.defaultValue || '';
      inp.addEventListener('keydown', e => e.stopPropagation());
      injectRow(inp);
      promptInput = inp;
      promptValidate = typeof opts.validate === 'function' ? opts.validate : null;
      if (promptValidate) {
        const why = document.createElement('div');
        why.className = 'confirm-prompt-reason';
        why.style.display = 'none';
        inp.parentElement.appendChild(why);
        promptReasonEl = why;
        inp.addEventListener('input', revalidatePrompt);
        revalidatePrompt();
      }
      setTimeout(() => { inp.focus(); inp.select(); }, 30);
    });
  }
}
define('stencil-confirm-modal', StencilConfirmModal);
