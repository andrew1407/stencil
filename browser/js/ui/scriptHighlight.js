// The .stc paint pass, shared by the script window (scriptModal.js) and the context menu's
// flyout (ctxScriptEditor.js). The colouring comes from the core's own token stream, so the
// editors and the runner never disagree about what a line means.
import { parseScript } from '../core/script.js';

/* The editor and the highlight layer share every metric, so a token's span in one lands on
 * the same pixel in the other. Diagnostics are painted only once the script has been RUN:
 * a half-typed line is not a mistake, and underlining it while you type reads as nagging. */
export const paintInto = (pre, text, withDiagnostics) => {
  const program = parseScript(text);
  while (pre.firstChild) pre.removeChild(pre.firstChild);

  const lines = text.split('\n');
  // Bucketed by line ONCE: the paint walks lines, and re-filtering every token per line is
  // quadratic on a long script. Tokens arrive in column order, so a diagnostic's own mark is
  // spliced in at its column and no bucket needs sorting.
  const byLine = Array.from({ length: lines.length + 1 }, () => []);
  const bucket = (line) => byLine[line] ?? (byLine[line] = []);
  for (const t of program.tokens) bucket(t.line).push({ col: t.col, len: t.len, cls: `stk-${t.kind}` });

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
  }

  lines.forEach((lineText, i) => {
    let at = 0;
    for (const m of bucket(i + 1)) {
      const start = m.col - 1;
      if (start < at || start > lineText.length) continue;
      if (start > at) pre.appendChild(document.createTextNode(lineText.slice(at, start)));
      const span = document.createElement('span');
      span.className = m.cls;
      span.textContent = lineText.slice(start, start + m.len);
      pre.appendChild(span);
      at = start + m.len;
    }
    if (at < lineText.length) pre.appendChild(document.createTextNode(lineText.slice(at)));
    if (i < lines.length - 1) pre.appendChild(document.createTextNode('\n'));
  });
  return program;
};

// The one diagnostic a surface has room for: the first error, else the first warning.
export const showDiagnostic = (strip, program) => {
  const first = program?.diagnostics.find((d) => d.severity === 'error')
    ?? program?.diagnostics.find((d) => d.severity === 'warning');
  strip.textContent = first ? `Line ${first.line}:${first.col} — ${first.message}` : '';
  strip.className = `script-diag${first ? ` script-diag-${first.severity}` : ''}`;
};
