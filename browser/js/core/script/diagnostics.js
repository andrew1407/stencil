// Port of core/script/diagnostics.cpp — diagnostic construction and the suggester.

const MAX_EDITS = 2;

// Capped at MAX_EDITS + 1, so a far-away candidate costs no more than a near one.
export const editDistance = (a, b) => {
  const cap = MAX_EDITS + 1;
  const n = a.length;
  const m = b.length;
  if (n > m + MAX_EDITS || m > n + MAX_EDITS) return cap;

  let prev = new Array(m + 1);
  let cur = new Array(m + 1);
  for (let j = 0; j <= m; j += 1) prev[j] = j;
  for (let i = 1; i <= n; i += 1) {
    cur[0] = i;
    let best = cur[0];
    for (let j = 1; j <= m; j += 1) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      cur[j] = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost);
      best = Math.min(best, cur[j]);
    }
    if (best > MAX_EDITS) return cap;
    const swap = prev;
    prev = cur;
    cur = swap;
  }
  return Math.min(prev[m], cap);
};

// The closest candidate within two edits, or '' when nothing is close enough.
export const didYouMean = (word, candidates) => {
  const needle = String(word).toLowerCase();
  let best = '';
  let bestScore = MAX_EDITS + 1;
  for (const c of candidates) {
    const d = editDistance(needle, String(c).toLowerCase());
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
