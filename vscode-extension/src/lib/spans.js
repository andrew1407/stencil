// Where a parser or CLI span lands in the editor. The .stc engine counts a column and a length
// in UTF-8 bytes from 1 (core/script, and so the parser copies and `--script-check`); VS Code
// counts UTF-16 units from 0. Every span is converted against the text of its own line.
import { columnIndex } from './parserHost.js';

// The buffer's lines, split where the lexer counts a new one.
const sourceLines = (text) => String(text ?? '').split('\n');

// A 1-based byte column as a 0-based unit index; past the line's end, a byte is one unit.
const unitOf = (text, index, col) => {
  const over = col - 1 - index.bytes;
  return over > 0 ? text.length + over : index.unitOf(col);
};
const unitColumn = (lineText, col) => unitOf(lineText ?? '', columnIndex(lineText ?? ''), col);

// One columnIndex per line of a `lines` array, so a document's spans cost one pass per line.
const indexes = new WeakMap();
const indexFor = (lines, line, text) => {
  let perLine = indexes.get(lines);
  if (!perLine) indexes.set(lines, (perLine = new Map()));
  let index = perLine.get(line);
  if (!index) perLine.set(line, (index = columnIndex(text)));
  return index;
};

// A 1-based (line, col, len) byte span as a 0-based line and [start, end) in UTF-16 units.
const unitSpan = (lines, { line, col, len }) => {
  const text = (lines ?? [])[line - 1] ?? '';
  const index = lines ? indexFor(lines, line, text) : columnIndex(text);
  const start = unitOf(text, index, col);
  return { line: line - 1, start, end: Math.max(start, unitOf(text, index, col + Math.max(0, len))) };
};

export { sourceLines, unitColumn, unitSpan };
