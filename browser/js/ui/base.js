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
    document.addEventListener('stencil:ready', e => this.wire(e.detail.app), { once: true });
  }

  // Overridden by subclasses that need behavior. `app` is the DrawingApp.
  wire(_app) {}
}

// Compose a host tag string for layout(): `<tag attrs>inner</tag>`.
export const hostTag = (tag, attrs, inner) => `<${tag}${attrs ? ' ' + attrs : ''}>${inner}</${tag}>`;

// ── Shared modal shell ──────────────────────────────────────────
// Wires open/close/overlay-mousedown/Escape for every app modal; returns
// { open, close }. onOpen/onClose run BEFORE the modal-open class toggles.
// `escapeClose` false (settingsModal) suppresses the bubble-phase Escape so its
// own capture-phase listener can handle it.
export const wireModalShell = (overlay, openBtn, closeBtn, { onOpen, onClose, escapeClose = true } = {}) => {
  const open = () => { onOpen?.(); overlay.classList.add('modal-open'); };
  const close = () => { onClose?.(); overlay.classList.remove('modal-open'); };
  if (openBtn) openBtn.addEventListener('click', open);
  if (closeBtn) closeBtn.addEventListener('click', close);
  overlay.addEventListener('mousedown', e => { if (e.target === overlay) close(); });
  if (escapeClose) {
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && overlay.classList.contains('modal-open')) close();
    });
  }
  return { open, close };
};

// Attach a search input that re-runs a filter on every keystroke.
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
// connected server, and show/hide its containing row by whether any server is
// connected. Returns true when at least one server target exists. Shared by the
// blank-image + links create modals so console (stencil.blank/load { address }) and
// UI thread the same address through the same code path (the parity rule).
export const fillTargetSelect = (selectEl, rowEl, connMgr) => {
  const urls = connMgr ? connMgr.urls : [];
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
