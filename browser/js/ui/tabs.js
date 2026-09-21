// The app's one tab strip: wraps an existing role="tablist" of [data-tab] buttons and the
// [data-panel] siblings they show, and announces the switch as `tab-change`. It toggles the
// class its host names, so each strip keeps the look its own stylesheet already gives it.
import { StencilElement, define } from './base.js';

const KEYS = { ArrowLeft: -1, ArrowUp: -1, ArrowRight: 1, ArrowDown: 1 };

export class StencilTabs extends StencilElement {
  #tabs = [];
  #active = '';

  get activeClass() { return this.getAttribute('active-class') || 'is-active'; }
  get active() { return this.#active; }

  wire() {
    this.#tabs = [...this.querySelectorAll('[role="tab"][data-tab]')];
    if (!this.#tabs.length) return;
    // The markup ships one tab already marked, so boot writes no DOM and moves no pixel.
    const marked = this.#tabs.find((t) => t.classList.contains(this.activeClass))
      || this.#tabs.find((t) => t.getAttribute('aria-selected') === 'true')
      || this.#tabs[0];
    this.#active = marked.dataset.tab;
    for (const t of this.#tabs) {
      t.addEventListener('click', () => this.select(t.dataset.tab));
      t.addEventListener('keydown', (e) => this.#onKey(e, t));
    }
  }

  // Panels live beside the strip, in the region that owns it.
  #panels() {
    const scope = this.closest('[data-panel-scope]') || this.parentElement || this;
    return [...scope.querySelectorAll('[data-panel]')];
  }

  // WAI-ARIA manual activation: arrows move focus only, so the panel's own field keeps the
  // caret the switch just gave it. Enter/Space activate through the button's native click.
  #onKey(e, tab) {
    const step = KEYS[e.key];
    const last = this.#tabs.length - 1;
    let to = null;
    if (step !== undefined) to = (this.#tabs.indexOf(tab) + step + this.#tabs.length) % this.#tabs.length;
    else if (e.key === 'Home') to = 0;
    else if (e.key === 'End') to = last;
    if (to === null) return;
    e.preventDefault();
    this.#tabs[to].focus();
  }

  // Always emits, even onto the tab already showing: a re-select is how the window resets.
  select(name) {
    const previous = this.#active;
    this.#active = name;
    for (const t of this.#tabs) {
      const on = t.dataset.tab === name;
      t.classList.toggle(this.activeClass, on);
      t.setAttribute('aria-selected', String(on));
    }
    for (const p of this.#panels()) p.style.display = p.dataset.panel === name ? '' : 'none';
    this.emit('tab-change', { tab: name, previous });
  }
}

define('stencil-tabs', StencilTabs);
