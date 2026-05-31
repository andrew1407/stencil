// ── Web Component base: light-DOM custom elements ───────────────
// Every UI region is a custom element that OWNS its markup (static inner())
// and its behavior (wire(app)). Rendering stays in light DOM so the app's
// global-id wiring, global CSS, and fullscreen cloneNode all keep working.
//
// Lifecycle: layout() emits each element's template() with markup inline, so
// connectedCallback finds children already present (renders only if created
// empty/programmatically). Behavior is app-dependent, so it waits for the
// one-shot `stencil:ready` event dispatched after DrawingApp is constructed —
// preserving the original DOM → app → wire init order.
//
// The modules are also imported by the Node test runner (which has no DOM), so
// we fall back to a plain base when HTMLElement is absent and no-op define()
// off-browser. Markup lives in pure static methods, so template()/inner() and
// layout() still work under Node — keeping the markup tests runnable.
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
