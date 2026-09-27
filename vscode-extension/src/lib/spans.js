// Where a parser or CLI span lands in the editor. The .stc engine counts a column and a length
// in UTF-8 bytes from 1 (core/script, and so the parser copies and `--script-check`); VS Code
// counts UTF-16 units from 0. Every span is converted against the text of its own line.
import { unitIndexOfColumn, utf8Length } from './parserHost.js';

// The buffer's lines, split where the lexer counts a new one.
const sourceLines = (text) => String(text ?? '').split('\n');

// A 1-based byte column as a 0-based unit index; past the line's end, a byte is one unit.
const unitColumn = (lineText, col) => {
  const text = lineText ?? '';
  const over = col - 1 - utf8Length(text);
  return over > 0 ? text.length + over : unitIndexOfColumn(text, col);
};

// A 1-based (line, col, len) byte span as a 0-based line and [start, end) in UTF-16 units.
const unitSpan = (lines, { line, col, len }) => {
  const text = (lines ?? [])[line - 1];
  const start = unitColumn(text, col);
  return { line: line - 1, start, end: Math.max(start, unitColumn(text, col + Math.max(0, len))) };
};

export { sourceLines, unitColumn, unitSpan };
