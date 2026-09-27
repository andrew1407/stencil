// Port of core/script/lexer.cpp. Owns two rules the rest of the language leans on:
// '#' opens a comment unless the token is a hex colour, and '://' never breaks a word.
import { MAX_LINES, MAX_TOKENS, utf8Length } from './types.js';

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

// The §1 number, -?\d+(\.\d+)?, as a prefix of `s`: its end, or 0 when there is none.
const numberEnd = (s) => {
  let i = s[0] === '-' ? 1 : 0;
  const digits = i;
  while (i < s.length && isDigit(s[i])) i += 1;
  if (i === digits) return 0;
  if (s[i] === '.' && isDigit(s[i + 1])) {
    i += 2;
    while (i < s.length && isDigit(s[i])) i += 1;
  }
  return i;
};

const isUnit = (u) => {
  const low = u.toLowerCase();
  return low === 'px' || low === 'cm' || low === 'mm' || low === 'in' || low === '%';
};

// "C:\a.png" and "C:/a.png": a drive letter's ':' belongs to the path, not the block.
const isDriveColon = (src, wordStart, j) => {
  const d = src[wordStart];
  return j === wordStart + 1 && ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z'))
    && (src[j + 1] === '\\' || src[j + 1] === '/');
};

const isParamWord = (s) => {
  if (s.length < 2 || s[0] !== '@') return false;
  for (let i = 1; i < s.length; i += 1) if (!isDigit(s[i])) return false;
  return true;
};

/* What one word lexes to by the §1 rules. `unitAt` is where a UNIT splits off a NUMBER, or
 * -1; a template argument is re-read through this, so a length stays a length. */
export const classifyWord = (word) => {
  if (!word) return { kind: 'ident', unitAt: -1 };
  if (word[0] === '#') return { kind: isHexColorWord(word) ? 'color' : 'ident', unitAt: -1 };
  if (isParamWord(word)) return { kind: 'param', unitAt: -1 };
  if (word[0] === '@') return { kind: 'directive', unitAt: -1 };
  const end = numberEnd(word);
  // Any other digit run ("10foo", "1.2.3", "0x10", "1e3") is a word, not a number.
  if (end > 0 && end === word.length) return { kind: 'number', unitAt: -1 };
  if (end > 0 && isUnit(word.slice(end))) return { kind: 'number', unitAt: end };
  return { kind: 'ident', unitAt: -1 };
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
    tokens.push({ line: atLine, col: atCol, len: utf8Length(t), kind, text: t });
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
          len: utf8Length(src, i, j),
          message: 'unterminated string — add a closing quote',
        });
      }
      push('string', val, startLine, startCol);
      col += utf8Length(src, i, j);
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
      if (src[j] === ':' && isDriveColon(src, i, j)) {
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
      col += utf8Length(src, i, k);
      i = k;
      continue;
    }

    const { kind, unitAt } = classifyWord(word);
    if (unitAt >= 0) {
      push('number', word.slice(0, unitAt), startLine, startCol);
      push('unit', word.slice(unitAt), startLine, startCol + unitAt);
      col += utf8Length(src, i, j);
      i = j;
      continue;
    }

    push(kind, word, startLine, startCol);
    col += utf8Length(src, i, j);
    i = j;
  }

  push('punct', '\n', line, col); // a virtual newline closes the last statement
  return { tokens, diagnostics };
};
