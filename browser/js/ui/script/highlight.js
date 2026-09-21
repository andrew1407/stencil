// The .stc paint pass, shared by the script window (modal.js) and the context menu's
// flyout (editor.js). The colouring comes from the core's own token stream, so the
// editors and the runner never disagree about what a line means.
import { parseScript } from '../../core/script.js';
import { DIRECTIVES } from '../../core/script/types.js';

// The lexer classifies on the '@' alone, so only a REAL directive is coloured, lowercased like
// the lowering (@CROP stays one). Read off the SOURCE: the wasm path leaves token.text empty.
const DIRECTIVE_WORDS = new Set(DIRECTIVES);
const knownDirective = (lines, t) =>
  DIRECTIVE_WORDS.has((lines[t.line - 1] ?? '').slice(t.col, t.col - 1 + t.len).toLowerCase());

/* The flat run of nodes a paint lands, in the order they sit in the <pre>: `cls` is empty
 * for a plain stretch and set for a span. Two adjacent plain stretches stay two entries,
 * because that is what the <pre> holds and the patch below compares against it. */
const segmentsOf = (lines, bucket) => {
  const segs = [];
  lines.forEach((lineText, i) => {
    let at = 0;
    for (const m of bucket(i + 1)) {
      const start = m.col - 1;
      if (start < at || start > lineText.length) continue;
      if (start > at) segs.push({ text: lineText.slice(at, start), cls: '' });
      segs.push({ text: lineText.slice(start, start + m.len), cls: m.cls });
      at = start + m.len;
    }
    if (at < lineText.length) segs.push({ text: lineText.slice(at), cls: '' });
    if (i < lines.length - 1) segs.push({ text: '\n', cls: '' });
  });
  return segs;
};

const spanOf = (text, cls) => {
  const span = document.createElement('span');
  span.className = cls;
  span.textContent = text;
  return span;
};

const same = (node, seg) => !!node && (seg.cls === '') === (node.nodeType === 3)
  && node.textContent === seg.text && (!seg.cls || node.className === seg.cls);

/* Only the stretch that actually changed is touched: the matching head and tail are found
 * first, so a keystroke rebuilds its own line and leaves the rest of the document's nodes
 * alone. Tearing the stream down made every span again. What lands is node for node what a
 * rebuild lands — the suite paints both ways and compares. */
const applyTo = (pre, segs) => {
  const nodes = pre.childNodes;
  let head = 0;
  while (head < segs.length && same(nodes[head], segs[head])) head += 1;
  let tail = 0;
  while (tail < segs.length - head && tail < nodes.length - head
      && same(nodes[nodes.length - 1 - tail], segs[segs.length - 1 - tail])) tail += 1;

  while (nodes.length - tail > head) pre.removeChild(nodes[head]);
  const stop = nodes[head] ?? null;
  for (let i = head; i < segs.length - tail; i += 1) {
    const { text, cls } = segs[i];
    pre.insertBefore(cls ? spanOf(text, cls) : document.createTextNode(text), stop);
  }
};

/* The editor and the highlight layer share every metric, so a token's span in one lands on
 * the same pixel in the other. Diagnostics are painted only once the script has been RUN:
 * a half-typed line is not a mistake, and underlining it while you type reads as nagging. */
export const paintInto = (pre, text, withDiagnostics) => {
  const program = parseScript(text);
  const lines = text.split('\n');
  // Bucketed by line ONCE: re-filtering every token per line is quadratic on a long script.
  // Tokens arrive in column order, so a diagnostic splices in at its column and nothing sorts.
  const byLine = Array.from({ length: lines.length + 1 }, () => []);
  const bucket = (line) => byLine[line] ?? (byLine[line] = []);
  for (const t of program.tokens) {
    if (t.kind === 'directive' && !knownDirective(lines, t)) continue;   // plain text, and still underlined
    bucket(t.line).push({ col: t.col, len: t.len, cls: `stk-${t.kind}` });
  }

  if (withDiagnostics) {
    for (const d of program.diagnostics) {
      const len = Math.max(1, d.len);
      const marks = bucket(d.line);
      // A diagnostic underlines the token already there rather than replacing it, so the
      // span keeps its colour AND gains the squiggle.
      const over = marks.filter((m) => m.col < d.col + len && d.col < m.col + Math.max(1, m.len));
      if (over.length > 0) { for (const m of over) m.cls += ` stk-${d.severity}`; continue; }
      let at = marks.length;
      while (at > 0 && marks[at - 1].col > d.col) at -= 1;
      marks.splice(at, 0, { col: d.col, len, cls: `stk-${d.severity}` });
    }
    // A bad line reads as bad WHOLE: the squiggle stays on the token the diagnostic names,
    // but every token on that line takes the danger ink, not just the offending one.
    for (const d of program.diagnostics) {
      if (d.severity !== 'error') continue;
      for (const m of bucket(d.line)) {
        if (!m.cls.includes('stk-error')) m.cls += ' stk-line-error';
      }
    }
  }

  applyTo(pre, segmentsOf(lines, bucket));
  return program;
};

// The one diagnostic a surface has room for: the first error, else the first warning.
export const showDiagnostic = (strip, program) => {
  const first = program?.diagnostics.find((d) => d.severity === 'error')
    ?? program?.diagnostics.find((d) => d.severity === 'warning');
  strip.textContent = first ? `Line ${first.line}:${first.col} — ${first.message}` : '';
  strip.className = `script-diag${first ? ` script-diag-${first.severity}` : ''}`;
};
