// The .stc paint pass, shared by the script window (scriptModal.js) and the context menu's
// flyout (ctxScriptEditor.js). The colouring comes from the core's own token stream, so the
// editors and the runner never disagree about what a line means.
import { parseScript } from '../core/script.js';

/* The editor and the highlight layer share every metric, so a token's span in one lands on
 * the same pixel in the other; only the classes differ. Diagnostics are painted only once
 * the script has been RUN: a half-typed line is not a mistake, and underlining it as one
 * while you are still writing reads as nagging. */
export const paintInto = (pre, text, withDiagnostics) => {
  const program = parseScript(text);
  while (pre.firstChild) pre.removeChild(pre.firstChild);

  const marks = program.tokens.map((t) => ({ line: t.line, col: t.col, len: t.len, cls: `stk-${t.kind}` }));
  if (withDiagnostics) {
    for (const d of program.diagnostics) {
      const len = Math.max(1, d.len);
      // A diagnostic underlines the token already there rather than replacing it, so the
      // span keeps its colour AND gains the squiggle.
      const over = marks.filter((m) => m.line === d.line
        && m.col < d.col + len && d.col < m.col + Math.max(1, m.len));
      if (over.length > 0) for (const m of over) m.cls += ` stk-${d.severity}`;
      else marks.push({ line: d.line, col: d.col, len, cls: `stk-${d.severity}` });
    }
  }

  const lines = text.split('\n');
  lines.forEach((lineText, i) => {
    const spans = marks.filter((m) => m.line === i + 1).sort((a, b) => a.col - b.col);
    let at = 0;
    for (const m of spans) {
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
