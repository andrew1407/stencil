// Port of core/script/diagnostics.cpp — diagnostic construction and the suggester.

const MAX_EDITS = 2;

// Past this a name gets no suggestion: nobody mistypes a word that long by two letters.
const MAX_SUGGEST_LENGTH = 64;

// Names compare as UTF-8 bytes folded on A-Z only, exactly as diagnostics.cpp compares them.
const utf8 = new TextEncoder();
const foldedBytes = (s) => utf8.encode(String(s)).map((b) => (b >= 65 && b <= 90 ? b + 32 : b));

/* Capped at MAX_EDITS + 1, so a far-away candidate costs no more than a near one: only the
 * diagonal band |i - j| <= MAX_EDITS can hold a distance under the cap, so only it is filled. */
const distance = (a, b) => {
  const cap = MAX_EDITS + 1;
  const n = a.length;
  const m = b.length;
  if (n > m + MAX_EDITS || m > n + MAX_EDITS) return cap;
  if (n > MAX_SUGGEST_LENGTH || m > MAX_SUGGEST_LENGTH) return cap;

  let prev = new Array(m + 2).fill(cap);
  let cur = new Array(m + 2).fill(cap);
  for (let j = 0; j <= Math.min(m, MAX_EDITS); j += 1) prev[j] = j;
  for (let i = 1; i <= n; i += 1) {
    const lo = Math.max(1, i - MAX_EDITS);
    const hi = Math.min(m, i + MAX_EDITS);
    cur[lo - 1] = lo === 1 ? Math.min(i, cap) : cap;
    let best = cur[lo - 1];
    for (let j = lo; j <= hi; j += 1) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      cur[j] = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost, cap);
      best = Math.min(best, cur[j]);
    }
    cur[hi + 1] = cap;
    if (best > MAX_EDITS) return cap;
    const swap = prev;
    prev = cur;
    cur = swap;
  }
  return Math.min(prev[m], cap);
};

export const editDistance = (a, b) => distance(utf8.encode(String(a)), utf8.encode(String(b)));

// The closest candidate within two edits, or '' when nothing is close enough.
export const didYouMean = (word, candidates) => {
  // A string past the limit in UTF-16 units is past it in bytes too, so it is never encoded.
  if (String(word).length > MAX_SUGGEST_LENGTH) return '';
  const needle = foldedBytes(word);
  let best = '';
  let bestScore = MAX_EDITS + 1;
  for (const c of candidates) {
    if (String(c).length > MAX_SUGGEST_LENGTH) continue;
    const d = distance(needle, foldedBytes(c));
    if (d < bestScore) {
      bestScore = d;
      best = c;
    }
  }
  return bestScore <= MAX_EDITS ? best : '';
};

export const makeDiag = (severity, code, at, message) => ({
  severity,
  code,
  line: at?.line ?? 1,
  col: at?.col ?? 1,
  len: at?.len ?? 0,
  message,
});

// 'file:line:col: error|warning: message [CODE]' — the line the editors parse.
export const formatDiagnostic = (file, d) =>
  `${file}:${d.line}:${d.col}: ${d.severity === 'error' ? 'error' : 'warning'}: ${d.message} [${d.code}]`;

export const hasErrors = (diagnostics) => diagnostics.some((d) => d.severity === 'error');

// A statement's position, as a token, for a diagnostic that points at the directive.
export const tokenOfStmt = (s) => ({
  line: s.line,
  col: s.col,
  len: s.len,
  text: `@${s.directive}`,
  kind: 'directive',
});

/* Diagnostics from `from` on came out of a template `call` ({ line, col, len }) expanded: each
 * names it and carries it as `related`; one already in `seen` (same code, same span) is dropped. */
export const noteCallSite = (diags, from, call, seen) => {
  let keep = from;
  for (let k = from; k < diags.length; k += 1) {
    const d = diags[k];
    if (d.line !== call.line || d.col !== call.col) {
      const key = `${d.code}:${d.line}:${d.col}`;
      if (seen.has(key)) continue;
      seen.add(key);
      d.message += ` (from the @use stencil at ${call.line}:${call.col})`;
      d.related = { line: call.line, col: call.col, len: call.len };
    }
    diags[keep] = d;
    keep += 1;
  }
  diags.length = keep;
};
