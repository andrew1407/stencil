import { StencilElement, hostTag, define, createModalFlight } from './base.js';
import { icon } from './icons.js';
import { gestureAnchorRect } from './gesturePoint.js';

// ── Component: generic confirm dialog ───────────────────────────
// A single reusable yes/no modal replacing native confirm(). Call via the
// instance method ask(message, opts) → Promise<boolean>; resolves true on OK,
// false on Cancel / Close / overlay-click / Escape. opts: { title, confirmLabel,
// cancelLabel, confirmIcon, danger }. app.confirm() (drawingApp) delegates here.
// `confirmIcon` (and askAlt's `altIcon`) name a glyph from ui/icons.js — pass one
// whenever the button says what it DOES, so the icon says the same thing.
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
    // The question is sand like every other window (ui/base.js createModalFlight) — but
    // it has no opener icon: it is raised by whatever the user just did, so it forms out
    // of motes streaming from THAT gesture's own point and pours back into it.
    const flight = createModalFlight(overlay, () => overlay.querySelector('.app-modal'));

    // The gesture this dialog grew from, captured on open and reused on close — measured
    // again at close time it would anchor on the dismiss button instead.
    let openAnchor = null;
    // …unless the caller names another way back (opts.closeAnchor): a context-menu row is
    // gone by close time, so the dust pours into the "⋯" the menu hung off. An element,
    // measured at close time, wherever the list has scrolled to by then.
    let closeAnchorEl = null;
    const rectOf = (el) => {
      const r = el?.getBoundingClientRect?.();
      return r && r.width > 0 && r.height > 0 ? r : null;
    };
    // Resolver for the in-flight ask()/choose(); null when no dialog is open.
    let resolveCurrent = null;
    // When set, the dialog is in "choose" mode: Confirm resolves with the picked
    // value, Cancel/Close/Escape resolve null (instead of the plain boolean).
    let choiceSelect = null;
    // When set, the dialog is in "prompt" mode: Confirm resolves the trimmed text.
    let promptInput = null;
    // Live validation (desktop twin: modalChrome.hpp PromptSpec::validate): the reason the
    // trimmed text cannot be accepted, '' when it can. A reason disables Confirm — Enter
    // with it too — and shows under the field, so the button is never a dead click.
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
    // Forget the last prompt's gate — the next dialog starts clickable.
    const clearPromptGate = () => {
      promptValidate = null;
      promptReasonEl = null;
      confirmBtn.disabled = false;
      delete confirmBtn.dataset.title;
    };
    // When set, the dialog has a THIRD button: Confirm resolves 'confirm', the extra
    // button 'alt', and Cancel/Close/Escape null (see askAlt).
    let altBtn = null;
    const settle = (val) => {
      // Measured while it is still up — display:none measures 0 — then handed to the
      // cloud, which has a life of its own on <body>: the answer never waits for it.
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
        // A multi-line prompt owns plain Enter — it types a newline — so only the
        // modifier form confirms there. Everywhere else Enter is still "OK".
        if (promptInput?.tagName === 'TEXTAREA' && e.target === promptInput && !(e.ctrlKey || e.metaKey)) return;
        e.preventDefault();
        if (promptWhyNot()) return;   // the same gate the disabled Confirm is behind
        settle(true);
      }
    };
    // Shared open: set labels/icon/danger, show the overlay, arm the key handler.
    const beginDialog = (message, opts, defaultTitle) => {
      document.getElementById('confirm-modal-title-text').textContent = opts.title || defaultTitle;
      // The header glyph says what KIND of dialog this is: the alert triangle for a
      // question with a consequence, `titleIcon` for anything else — a prompt that just
      // collects a value (keywords, a description) is information, not a warning.
      document.getElementById('confirm-modal-title-icon').innerHTML =
        icon(opts.titleIcon || 'alert', { size: 18 });
      document.getElementById('confirm-modal-message').textContent = message || '';
      // The glyph follows the ACTION, not the dialog: a plain yes/no keeps the check,
      // but a named action ("Replace") shows what it does instead of a generic tick.
      confirmBtn.innerHTML =
        icon(opts.confirmIcon || 'check', { size: 14 }) + '<span id="confirm-modal-confirm-text"></span>';
      document.getElementById('confirm-modal-confirm-text').textContent = opts.confirmLabel || 'OK';
      document.getElementById('confirm-modal-cancel-text').textContent = opts.cancelLabel || 'Cancel';
      confirmBtn.classList.toggle('danger', !!opts.danger);
      flight.finishClose();   // a question asked while the last one is still leaving
      overlay.classList.add('modal-open');
      // Measured after the class applies — the box has no size while display:none. Captured
      // once here and reused by settle() so the close flies back to this same point.
      openAnchor = gestureAnchorRect();
      closeAnchorEl = opts.closeAnchor || null;
      if (!flight.reducedMotion() && flight.setOrigin(openAnchor)) flight.playDust(true);
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
        prev(choiceSelect || promptInput || altBtn ? null : false);
      }
      if (choiceSelect) { choiceSelect.parentElement?.remove(); choiceSelect = null; }
      if (promptInput) { promptInput.parentElement?.remove(); promptInput = null; }
      clearPromptGate();
      if (altBtn) { altBtn.remove(); altBtn = null; }
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

    // Three-button variant: Cancel | <altLabel> | <confirmLabel>. Resolves 'confirm',
    // 'alt', or null — for a question with two real answers plus a way out, like
    // "combine this layout with the existing lines, or replace them?".
    // opts: { title, confirmLabel, altLabel, cancelLabel, confirmIcon, altIcon, danger }.
    this.askAlt = (message, opts = {}) => new Promise(resolve => {
      dismissPrevious();
      resolveCurrent = resolve;
      beginDialog(message, opts, 'Confirm');
      // Injected at runtime, like the picker/prompt rows, so the static markup (and the
      // markup tests) stay unchanged.
      const btn = document.createElement('button');
      btn.id = 'confirm-modal-alt';
      btn.className = 'btn-icon-text';
      // Carries a glyph like the other two — a bare word beside two icon buttons
      // reads as the odd one out rather than as an equal choice.
      btn.innerHTML = icon(opts.altIcon || 'plus', { size: 14 }) + '<span></span>';
      btn.querySelector('span').textContent = opts.altLabel || 'Alternative';
      btn.addEventListener('click', () => settle('alt'));
      cancelBtn.parentElement.insertBefore(btn, cancelBtn.nextSibling);
      altBtn = btn;
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

    // Text-prompt variant: an <input> below the message, resolving the trimmed text on
    // Confirm, null otherwise. opts: { title, titleIcon, confirmLabel, defaultValue,
    // multiline, rows, validate }. `multiline` swaps in a `rows`-tall <textarea>
    // (default 3) for sentence-shaped values; Enter then types a newline and Ctrl/⌘+Enter
    // saves. `validate(trimmed)` returns the reason the value cannot be accepted ('' when
    // it can) — it disables Confirm and Enter and shows under the field.
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
      inp.addEventListener('keydown', e => e.stopPropagation());   // keep the modal's Enter/Esc, but let typing through
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
        revalidatePrompt();   // an empty box starts refused, not offering a dead button
      }
      setTimeout(() => { inp.focus(); inp.select(); }, 30);
    });
  }
}
define('stencil-confirm-modal', StencilConfirmModal);
