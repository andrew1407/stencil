// The console's scrollback: one line per span, text only (never markup), each new line swept in
// from the left by the reveal plan — the window and the CLI stage share it.
import { createReveal } from './reveal.js';
import { consoleSpeed, motionReduced } from '../motion/motionPrefs.js';

const MAX_LINES = 5000;   // the Zig console's scrollback cap (screen.zig)
const KIND_CLASS = { echo: 'console-line-echo', error: 'console-line-error', help: 'console-line-help', secret: 'console-line-secret', title: 'console-line-title', plain: '' };

const textOf = (line) => (typeof line === 'string' ? line : line.text);
const SWEPT = new Set(['plain', 'error', 'secret', 'title', 'help']);   // the echo lands at once, as the Zig console's does
// The run a line paints apart: a help row's command, an echo's prompt and word, the `error: ` prefix.
const headOf = (line, kind) => (typeof line !== 'string' ? line.head ?? '' : kind === 'error' && line.startsWith('error: ') ? 'error: ' : '');
const HEAD_CLASS = { error: 'console-line-error-prefix' };

const RUNS = '.console-line-cmd, .console-line-section';   // the runs in the accent a skin re-dresses
const plainText = (el, text) => { el.textContent = text; };

export const createConsoleView = (pre, { reveal = createReveal({ speed: consoleSpeed }), now = () => performance.now(),
                                         schedule = (fn, ms) => setTimeout(fn, ms) } = {}) => {
  const pending = new Map();   // el → row, while its print is still typing in
  const queue = [];            // prints waiting their turn, each an array of rows
  let settled = () => {};
  let decorate = plainText;
  // An accent run as shown so far: the decorator fills it, the text kept for a later re-dress.
  const show = (el, text, full) => { el.dataset.shown = text; decorate(el, text, full); };

  const doc = pre.ownerDocument || globalThis.document;
  // One line as appended: `cut(x)` shows its first x characters, a help row's bold head included.
  const append = (line, kind) => {
    const el = doc.createElement('span');
    const text = textOf(line);
    el.dataset.text = text;
    el.className = `console-line ${KIND_CLASS[kind] ?? ''}`.trim();
    const section = typeof line !== 'string' && line.kind === 'section';
    if (section) el.classList.add('console-line-section');
    if (typeof line !== 'string' && line.indent) el.style.marginLeft = `${line.indent}ch`;
    const headText = headOf(line, kind);
    let cut;
    if (headText) {
      const head = doc.createElement('span');
      head.className = HEAD_CLASS[kind] ?? 'console-line-cmd';
      const tail = doc.createTextNode('');
      el.appendChild(head);
      el.appendChild(tail);
      const fill = kind === 'error' ? plainText : show;   // `error: ` is red, never the accent
      cut = (x) => { fill(head, headText.slice(0, x), text); tail.nodeValue = text.slice(headText.length, Math.max(headText.length, x)); };
    } else if (section) {
      cut = (x) => show(el, text.slice(0, x), text);
    } else {
      cut = (x) => plainText(el, text.slice(0, x));
    }
    cut(Infinity);
    pre.appendChild(el);
    while (pre.childElementCount > MAX_LINES) pre.firstElementChild.remove();
    return { el, text, cut };
  };

  // The scrollback stays at its end as lines type in: a swept row is empty when it lands.
  const follow = () => { pre.scrollTop = pre.scrollHeight; };
  // One row at a time, top to bottom, as the Zig console prints a block line by line and reveals
  // each (reveal.zig revealNew): the first after a pause in the long form, the rows pouring after
  // it in the burst form, and past the burst window the rest land at once.
  const pump = () => {
    if (pending.size || !queue.length) return;
    const rows = queue.shift();
    const reach = Math.max(0, ...rows.map((r) => r.text.length));
    const { jumps, stepMs } = reveal.plan(now());
    if (!jumps || motionReduced() || !reach) { rows.forEach((r) => r.cut(Infinity)); reveal.done?.(now()); follow(); pump(); return; }
    const per = Math.max(1, Math.ceil(reach / jumps));
    rows.forEach((r) => pending.set(r.el, r));
    let x = per;
    const step = () => {
      if (!rows.some((r) => pending.has(r.el))) return;
      rows.forEach((r) => r.cut(x));
      follow();
      x += per;
      if (x < reach) schedule(step, stepMs);
      else settle(rows);
    };
    schedule(step, stepMs);
  };
  const settle = (rows) => {
    rows.forEach((r) => { if (pending.delete(r.el)) r.cut(Infinity); });
    reveal.done?.(now());
    follow();
    if (!pending.size && queue.length) pump();
    if (!pending.size && !queue.length) settled();
  };
  // A keystroke lands every line whole at once, as the Zig console's queued input does.
  const settleNow = () => {
    queue.splice(0).forEach((rows) => rows.forEach((r) => r.cut(Infinity)));
    if (pending.size) settle([...pending.values()]); else follow();
  };

  return {
    print(lines, kind = 'plain') {
      if (!lines.length) return;
      const rows = lines.map((l) => append(l, kind));
      if (SWEPT.has(kind)) { rows.forEach((r) => { r.cut(0); queue.push([r]); }); pump(); }
      follow();
    },
    clear() { pending.clear(); queue.length = 0; while (pre.firstChild) pre.removeChild(pre.firstChild); reveal.reset(); },
    // The text as it stands, revealed or not: what a test or a copy reads.
    lines: () => [...pre.children].map((el) => el.dataset.text),
    revealing: () => pending.size > 0 || queue.length > 0,
    settleNow,
    onSettled(fn) { settled = fn; },
    // How an accent run is filled: `fn(el, shown, lineText)`; null is plain text again.
    setDecorator(fn) { decorate = fn ?? plainText; },
    // Every accent run in the scrollback, for a re-dress when the skin changes or ticks.
    runs: () => [...pre.querySelectorAll(RUNS)],
  };
};
