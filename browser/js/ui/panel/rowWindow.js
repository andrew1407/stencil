// A long list body as a window: the rows its scroller shows, and OVERSCAN either side, between two
// spacers standing in for the rest, so an edit lays out the rows on screen rather than all of them.
// The shape (tableWindow.js here, the extension's popup/list/rows.js) supplies the row tag, the spacer
// and the scroller. Extension twin: browser-extension/src/lib/virtualList/rowWindow.js (byte-equal).

export const VIRTUAL_ROWS = 500;
const OVERSCAN = 20;
const GUESS_ROW_PX = 28;   // a 12px line in 6px padding and a 1px rule: replaced once a row is measured
const SPACER_CLASS = 'row-window-spacer';

export class RowWindow {
  #rowPx = GUESS_ROW_PX;
  #count = 0;
  #span = null;
  #wired = false;
  #frame = 0;
  #sizerRow = null;

  // `build(i)` makes row i, which carries its index in `indexAttr` (a spacer carries none).
  // `sizer()`, when given, is an unseen last row that holds the columns at the whole list's widths.
  // While `isHeld()` (a row being dragged), scrolling renders nothing, so the held row is never dropped.
  constructor(body, { shape, indexAttr, build, sizer = null, onRender = () => {}, threshold = VIRTUAL_ROWS,
                      isHeld = () => false }) {
    Object.assign(this, { body, shape, indexAttr, build, sizer, onRender, threshold, isHeld,
      rowSelector: `${shape.tag}[${indexAttr}]` });
  }

  get active() { return this.#count > this.threshold; }
  get count() { return this.#count; }

  // False for a short list (the caller lists every row as it always did); else the window, rendered.
  show(count) {
    this.#count = count;
    if (!this.active) { this.#span = null; this.#sizerRow = null; return false; }
    this.#sizerRow = this.sizer?.() ?? null;
    this.#wire();
    this.#render(true);
    return true;
  }

  #scroller() { return this.shape.scroller(this.body) ?? null; }

  // No stripe, hover wash or pointer: it is empty height, not a row.
  #spacer(px) {
    const node = this.shape.spacer(px);
    node.className = SPACER_CLASS;
    node.setAttribute('aria-hidden', 'true');
    for (const [k, v] of [['background-color', 'transparent'], ['cursor', 'default'], ['pointer-events', 'none']])
      node.style.setProperty(k, v, 'important');
    return node;
  }

  #wire() {
    const scroller = this.#scroller();
    if (this.#wired || !scroller?.addEventListener) return;
    this.#wired = true;
    scroller.addEventListener('scroll', () => this.#follow(), { passive: true });
    if (typeof ResizeObserver === 'function') new ResizeObserver(() => this.#follow()).observe(scroller);
  }

  // Scroll and resize land many times a frame; the window follows once a frame. A hidden body (another
  // tab of a shared scroller) may hold a stale count, so it waits for its caller's next show().
  #follow() {
    const step = () => {
      this.#frame = 0;
      if (this.active && this.body.checkVisibility?.() !== false) this.#render(false);
    };
    if (typeof requestAnimationFrame !== 'function') { step(); return; }
    this.#frame ||= requestAnimationFrame(step);
  }

  // Row `i`'s top inside the scroller's content; a body that is its own scroller starts at 0.
  #rowTop(scroller, i) {
    const bodyTop = scroller === this.body ? 0
      : this.body.getBoundingClientRect().top - scroller.getBoundingClientRect().top + scroller.scrollTop;
    return bodyTop + i * this.#rowPx;
  }

  #span0() {
    const scroller = this.#scroller();
    const shown = scroller ? Math.ceil(scroller.clientHeight / this.#rowPx) : 40;
    const top = scroller ? Math.floor((scroller.scrollTop - this.#rowTop(scroller, 0)) / this.#rowPx) : 0;
    let first = Math.max(0, Math.min(this.#count - 1, top) - OVERSCAN);
    if (this.shape.stripes && first > 0 && first % 2 === 0) first -= 1;   // row i stays on its own stripe (nth-child counts the spacer)
    return { first, last: Math.min(this.#count - 1, Math.max(0, top) + shown + OVERSCAN) };
  }

  #render(force) {
    const span = this.#span0();
    if (!force && this.#span && span.first === this.#span.first && span.last === this.#span.last) return;
    if (!force && (this.body.querySelector?.('input') || this.isHeld())) return;   // an open cell editor keeps its row
    this.#span = span;
    const frag = document.createDocumentFragment();
    if (span.first > 0) frag.appendChild(this.#spacer(span.first * this.#rowPx));
    for (let i = span.first; i <= span.last; i++) frag.appendChild(this.build(i));
    if (span.last < this.#count - 1) frag.appendChild(this.#spacer((this.#count - 1 - span.last) * this.#rowPx));
    if (this.#sizerRow) frag.appendChild(this.#sizerRow);
    this.body.replaceChildren(frag);
    const rows = this.body.querySelectorAll(this.rowSelector);
    const measured = rows.length > 1 ? (rows[rows.length - 1].offsetTop - rows[0].offsetTop) / (rows.length - 1) : 0;
    if (measured > 0 && Math.abs(measured - this.#rowPx) > 0.5) {
      this.#rowPx = measured;
      this.#render(true);
      return;
    }
    this.onRender();
  }

  // Scrolls row `i` clear of a sticky header and into view, as scrollIntoView 'nearest' would, and
  // renders it; the row (null while a smooth scroll is still on its way, its scroll events render it).
  reveal(i, behavior = 'auto') {
    const scroller = this.#scroller();
    if (!this.active || !scroller) return null;
    const top = this.#rowTop(scroller, i);
    const headPx = this.shape.headPx?.(this.body) ?? 0;
    let to = null;
    if (top < scroller.scrollTop + headPx) to = top - headPx;
    else if (top + this.#rowPx > scroller.scrollTop + scroller.clientHeight) to = top + this.#rowPx - scroller.clientHeight;
    if (to !== null && behavior === 'smooth' && scroller.scrollTo) {
      scroller.scrollTo({ top: to, behavior });
      return null;
    }
    // 'instant', not a scrollTop write: under CSS scroll-behavior:smooth that write animates and row i is not rendered yet.
    if (to !== null && scroller.scrollTo) scroller.scrollTo({ top: to, behavior: 'instant' });
    else if (to !== null) scroller.scrollTop = to;
    this.#render(true);
    return this.rendered(i);
  }

  rendered(i) { return this.body.querySelector?.(`${this.shape.tag}[${this.indexAttr}="${i}"]`) ?? null; }
}
