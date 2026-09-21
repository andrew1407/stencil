import { onReady } from '../eventBus/appBus.js';
// Light-DOM custom elements: each region owns its markup (static inner()) and behaviour
// (wire(app), after the one-shot `stencil:ready`). Off-browser this is a plain base and a
// no-op define(), so the Node runner can import markup.
const ElementBase = typeof HTMLElement !== 'undefined' ? HTMLElement : class {};

export const define = (tag, klass) => {
  if (typeof customElements !== 'undefined') customElements.define(tag, klass);
};

export class StencilElement extends ElementBase {
  #wired = false;

  connectedCallback() {
    if (this.#wired) return;
    this.#wired = true;
    if (!this.firstElementChild && this.constructor.inner) this.innerHTML = this.constructor.inner();
    onReady((app) => this.wire(app));
  }

  // Scoped to this element: a region never reaches another region's nodes by id.
  $(id) { return this.querySelector(`#${id}`); }

  emit(type, detail) {
    return this.dispatchEvent(new CustomEvent(type, { detail, bubbles: true }));
  }

  wire(_app) {}
}

export const hostTag = (tag, attrs, inner) => `<${tag}${attrs ? ' ' + attrs : ''}>${inner}</${tag}>`;

// For any server-supplied or user-typed value interpolated into innerHTML.
export { escapeHtml } from './escapeHtml.js';

export { closeOpenModal } from './modal/modalRegistry.js';
export { MODAL_CLOSE_MS, createModalFlight } from './modal/modalFlight.js';
export { wireModalShell } from './modal/modalShell.js';

export const attachSearchFilter = (searchInput, applyFilterFn) => {
  searchInput.addEventListener('input', applyFilterFn);
};

// Empty query matches everything; otherwise case-insensitive substring.
export const rowMatches = (text, query) => {
  const q = String(query ?? '').trim().toLowerCase();
  return !q || String(text ?? '').toLowerCase().includes(q);
};

// "Local" (value "") plus one option per connected server; `allow` false (incognito)
// suppresses every server target.
export const fillTargetSelect = (selectEl, rowEl, connMgr, allow = true) => {
  const urls = (allow && connMgr) ? connMgr.urls : [];
  selectEl.innerHTML = '';
  const local = document.createElement('option');
  local.value = '';
  local.textContent = 'Local (this browser)';
  selectEl.appendChild(local);
  for (const url of urls) {
    const opt = document.createElement('option');
    opt.value = url;
    opt.textContent = url;
    selectEl.appendChild(opt);
  }
  if (rowEl) rowEl.style.display = urls.length ? '' : 'none';
  return urls.length > 0;
};
