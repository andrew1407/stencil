// A long table body as a window: the rows its scroller shows, and OVERSCAN either side, between two
// spacer rows standing in for the rest, so an edit lays out the rows on screen rather than all of them.
// Shared by the points table (coordTable.js) and the lines list (lines/list.js); a short one is untouched.
import { perFrame } from '../../utils.js';

export const VIRTUAL_ROWS = 500;
const OVERSCAN = 20;
const GUESS_ROW_PX = 28;   // a 12px line in 6px padding and a 1px rule: replaced once a row is measured

const spacer = (columns, px) => {
  const row = document.createElement('tr');
  row.className = 'row-window-spacer';
  row.setAttribute('aria-hidden', 'true');
  // No stripe, hover wash or pointer: it is empty height, not a row.
  for (const [k, v] of [['background-color', 'transparent'], ['cursor', 'default'], ['pointer-events', 'none']])
    row.style.setProperty(k, v, 'important');
  const cell = document.createElement('td');
  cell.colSpan = columns;
  cell.style.cssText = `height:${px}px;padding:0;border:0;`;
  row.appendChild(cell);
  return row;
};

export class RowWindow {
  #rowPx = GUESS_ROW_PX;
  #count = 0;
  #span = null;
  #wired = false;
  #sizerRow = null;

  // `build(i)` makes row i, which carries its index in `indexAttr` (a spacer carries none).
  // `sizer()`, when given, is an unseen last row that holds the columns at the whole list's widths.
  constructor(body, { columns, indexAttr, build, sizer = null, onRender = () => {} }) {
    Object.assign(this, { body, columns, indexAttr, build, sizer, onRender, rowSelector: `tr[${indexAttr}]` });
  }

  get active() { return this.#count > VIRTUAL_ROWS; }
  get count() { return this.#count; }

  #scroller() { return this.body.closest?.('#coord-body') ?? null; }

  // False for a short list (the caller lists every row as it always did); else the window, rendered.
  show(count) {
    this.#count = count;
    if (!this.active) { this.#span = null; this.#sizerRow = null; return false; }
    this.#sizerRow = this.sizer?.() ?? null;
    this.#wire();
    this.#render(true);
    return true;
  }

  #wire() {
    const scroller = this.#scroller();
    if (this.#wired || !scroller?.addEventListener) return;
    this.#wired = true;
    const follow = perFrame(() => { if (this.active) this.#render(false); });
    scroller.addEventListener('scroll', follow, { passive: true });
    if (typeof ResizeObserver === 'function') new ResizeObserver(follow).observe(scroller);
  }

  // Row `i`'s top inside the scroller's content.
  #rowTop(scroller, i) {
    const bodyTop = this.body.getBoundingClientRect().top - scroller.getBoundingClientRect().top + scroller.scrollTop;
    return bodyTop + i * this.#rowPx;
  }

  #span0() {
    const scroller = this.#scroller();
    const shown = scroller ? Math.ceil(scroller.clientHeight / this.#rowPx) : 40;
    const top = scroller ? Math.floor((scroller.scrollTop - this.#rowTop(scroller, 0)) / this.#rowPx) : 0;
    let first = Math.max(0, Math.min(this.#count - 1, top) - OVERSCAN);
    if (first > 0 && first % 2 === 0) first -= 1;   // row i stays on its own stripe (nth-child counts the spacer)
    return { first, last: Math.min(this.#count - 1, Math.max(0, top) + shown + OVERSCAN) };
  }

  #render(force) {
    const span = this.#span0();
    if (!force && this.#span && span.first === this.#span.first && span.last === this.#span.last) return;
    if (!force && this.body.querySelector?.('input')) return;   // an open cell editor keeps its row
    this.#span = span;
    const frag = document.createDocumentFragment();
    if (span.first > 0) frag.appendChild(spacer(this.columns, span.first * this.#rowPx));
    for (let i = span.first; i <= span.last; i++) frag.appendChild(this.build(i));
    if (span.last < this.#count - 1) frag.appendChild(spacer(this.columns, (this.#count - 1 - span.last) * this.#rowPx));
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

  // Scrolls row `i` clear of the sticky header and into view, as scrollIntoView 'nearest' would, and
  // renders it; the row (null while a smooth scroll is still on its way, its scroll events render it).
  reveal(i, behavior = 'auto') {
    const scroller = this.#scroller();
    if (!this.active || !scroller) return null;
    const top = this.#rowTop(scroller, i);
    const headPx = this.body.parentElement?.tHead?.offsetHeight ?? 0;
    let to = null;
    if (top < scroller.scrollTop + headPx) to = top - headPx;
    else if (top + this.#rowPx > scroller.scrollTop + scroller.clientHeight) to = top + this.#rowPx - scroller.clientHeight;
    if (to !== null && behavior === 'smooth' && scroller.scrollTo) {
      scroller.scrollTo({ top: to, behavior });
      return null;
    }
    if (to !== null) scroller.scrollTop = to;
    this.#render(true);
    return this.rendered(i);
  }

  rendered(i) { return this.body.querySelector?.(`tr[${this.indexAttr}="${i}"]`) ?? null; }
}
