// Port of core/script/scriptLexer.cpp. Owns two rules the rest of the language leans on:
// '#' opens a comment unless the token is a hex colour, and '://' never breaks a word.
import { MAX_LINES, MAX_TOKENS } from './scriptTypes.js';

const isSpace = (c) => c === ' ' || c === '\t' || c === '\n' || c === '\r' || c === '\f' || c === '\v';
const isDigit = (c) => c >= '0' && c <= '9';
const isHexByte = (c) => isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');

const isWordByte = (c) =>
  !isSpace(c) && c !== ',' && c !== ';' && c !== '#' && c !== ':' && c !== '(' && c !== ')' &&
  c !== '=' && c !== '"';

export const isHexColorWord = (word) => {
  if (word.length < 2 || word[0] !== '#') return false;
  const n = word.length - 1;
  if (n !== 3 && n !== 4 && n !== 6 && n !== 8) return false;
  for (let i = 1; i < word.length; i += 1) if (!isHexByte(word[i])) return false;
  return true;
};

const looksNumeric = (s) => {
  let i = s[0] === '-' || s[0] === '+' ? 1 : 0;
  let digit = false;
  for (; i < s.length; i += 1) {
    if (isDigit(s[i])) digit = true;
    else if (s[i] === '.') continue;
    else break;
  }
  return digit;
};

// A number may carry a unit; the run is one NUMBER token plus a UNIT token.
const unitSuffix = (s) => {
  let i = s[0] === '-' || s[0] === '+' ? 1 : 0;
  while (i < s.length && (isDigit(s[i]) || s[i] === '.')) i += 1;
  if (i >= s.length) return -1;
  const u = s.slice(i).toLowerCase();
  return u === 'px' || u === 'cm' || u === 'mm' || u === 'in' || u === '%' ? i : -1;
};

const isParamWord = (s) => {
  if (s.length < 2 || s[0] !== '@') return false;
  for (let i = 1; i < s.length; i += 1) if (!isDigit(s[i])) return false;
  return true;
};

export const lexScript = (text) => {
  const tokens = [];
  const diagnostics = [];
  const src = String(text ?? '');

  let line = 1;
  let col = 1;
  let i = 0;
  let capped = false;
  // Past the cap the rest of the file is dropped, and the first token dropped says so.
  const push = (kind, t, atLine, atCol) => {
    if (tokens.length >= MAX_TOKENS) {
      if (capped) return;
      capped = true;
      diagnostics.push({
        severity: 'error',
        code: 'E_LIMIT_TOKENS',
        line: atLine,
        col: atCol,
        len: 0,
        message: `script has too many tokens (over ${MAX_TOKENS})`,
      });
      return;
    }
    tokens.push({ line: atLine, col: atCol, len: t.length, kind, text: t });
  };

  while (i < src.length) {
    const c = src[i];
    if (c === '\r') { i += 1; continue; }
    if (c === '\n') {
      push('punct', '\n', line, col);
      i += 1;
      line += 1;
      col = 1;
      if (line > MAX_LINES) {
        diagnostics.push({
          severity: 'error',
          code: 'E_LIMIT_LINES',
          line,
          col: 1,
          len: 0,
          message: `script is too long (over ${MAX_LINES} lines)`,
        });
        return { tokens, diagnostics };
      }
      continue;
    }
    if (isSpace(c)) { i += 1; col += 1; continue; }

    const startLine = line;
    const startCol = col;

    if (c === '"') {
      let val = '"';
      let j = i + 1;
      let run = j; // start of the plain run since the last escape
      let closed = false;
      while (j < src.length && src[j] !== '\n') {
        if (src[j] === '\\' && (src[j + 1] === '"' || src[j + 1] === '\\')) {
          val += src.slice(run, j) + src[j + 1];
          j += 2;
          run = j;
          continue;
        }
        if (src[j] === '"') { closed = true; break; }
        j += 1;
      }
      val += `${src.slice(run, j)}"`;
      if (closed) j += 1;
      if (!closed) {
        diagnostics.push({
          severity: 'error',
          code: 'E_UNTERMINATED_STRING',
          line: startLine,
          col: startCol,
          len: j - i,
          message: 'unterminated string — add a closing quote',
        });
      }
      push('string', val, startLine, startCol);
      col += j - i;
      i = j;
      continue;
    }

    if (c === ',' || c === ':' || c === '(' || c === ')' || c === '=' || c === ';') {
      push('punct', c, startLine, startCol);
      i += 1;
      col += 1;
      continue;
    }

    // A word runs to whitespace or punctuation. '#' only breaks a word when it is not the
    // word's own first byte, so "#ccc" stays whole and "a#b" splits.
    let j = c === '#' ? i + 1 : i;
    let scheme = false;
    while (j < src.length) {
      if (src[j] === ':' && src[j + 1] === '/' && src[j + 2] === '/') {
        scheme = true;
        j += 3;
        continue;
      }
      // So does a port's ':', once the word already carries a scheme — the block's own
      // ':' is never followed by a digit, and "aspect=3:2" carries no scheme.
      if (src[j] === ':' && isDigit(src[j + 1]) && scheme) {
        j += 1;
        continue;
      }
      if (!isWordByte(src[j])) break;
      j += 1;
    }
    const word = src.slice(i, j);

    if (c === '#' && !isHexColorWord(word)) {
      let k = i;
      while (k < src.length && src[k] !== '\n') k += 1;
      push('comment', src.slice(i, k), startLine, startCol);
      col += k - i;
      i = k;
      continue;
    }

    let kind = 'ident';
    if (word[0] === '#') {
      kind = 'color';
    } else if (isParamWord(word)) {
      kind = 'param';
    } else if (word[0] === '@') {
      kind = 'directive';
    } else if (looksNumeric(word)) {
      const unitAt = unitSuffix(word);
      if (unitAt >= 0) {
        push('number', word.slice(0, unitAt), startLine, startCol);
        push('unit', word.slice(unitAt), startLine, startCol + unitAt);
        col += j - i;
        i = j;
        continue;
      }
      kind = 'number';
    }

    push(kind, word, startLine, startCol);
    col += j - i;
    i = j;
  }

  push('punct', '\n', line, col); // a virtual newline closes the last statement
  return { tokens, diagnostics };
};
