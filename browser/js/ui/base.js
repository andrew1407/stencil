import { onReady } from '../bus/appBus.js';
// ── Web Component base: light-DOM custom elements ───────────────
// Each UI region owns its markup (static inner()) and behavior (wire(app)). Light
// DOM keeps global-id wiring, global CSS, and fullscreen cloneNode working. wire()
// waits for the one-shot `stencil:ready` (fired after DrawingApp exists) to preserve
// DOM → app → wire init order. Falls back to a plain base + no-op define() off-browser
// so the Node test runner (no DOM) can still import markup.
const ElementBase = typeof HTMLElement !== 'undefined' ? HTMLElement : class {};

// Register a custom element, but only in a browser (no customElements in Node).
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

  // Overridden by subclasses that need behavior. `app` is the DrawingApp.
  wire(_app) {}
}

// Compose a host tag string for layout(): `<tag attrs>inner</tag>`.
export const hostTag = (tag, attrs, inner) => `<${tag}${attrs ? ' ' + attrs : ''}>${inner}</${tag}>`;

// Escape a string for safe interpolation into an innerHTML template. Use it for any
// value that can carry server-supplied or user-typed text (project names, server
// URLs/addresses) so a crafted value can't inject markup/script. Non-strings coerce.
export { escapeHtml } from './escapeHtml.js';

// The shared modal shell and its flight are their own modules; re-exported here so
// every component keeps importing its window machinery from one place.
export { closeOpenModal } from './modalRegistry.js';
export { MODAL_CLOSE_MS, createModalFlight } from './modalFlight.js';
export { wireModalShell } from './modalShell.js';

export const attachSearchFilter = (searchInput, applyFilterFn) => {
  searchInput.addEventListener('input', applyFilterFn);
};

// Pure per-row search predicate: empty/whitespace query matches everything;
// otherwise case-insensitive substring match. Trims the query internally so
// callers don't have to.
export const rowMatches = (text, query) => {
  const q = String(query ?? '').trim().toLowerCase();
  return !q || String(text ?? '').toLowerCase().includes(q);
};

// Populate a <select> with create targets: "Local" (value "") plus one option per
// connected server, showing/hiding its row. Shared by the create modals so console
// (stencil.blank/load { address }) and UI thread the same address (the parity rule).
// `allow` false (incognito) suppresses every server target — incognito content must
// not be created on a server.
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
