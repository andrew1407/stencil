// The .stc engine reports a column and a length in UTF-8 bytes, and VS Code places a range in
// UTF-16 units. On a line holding é, 日本 or 😀 the two differ, so every consumer is driven over
// such a line here: each range must slice back to exactly the characters the engine meant.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript } from '../src/parser/index.js';
import { unitIndexOfColumn, utf8Length } from '../src/parser/script/types.js';
import { sourceLines, unitColumn, unitSpan } from '../src/lib/spans.js';
import { installVscodeStub, makeContext, makeDocument, makeEditor, makeVscode } from './helpers/vscodeStub.js';

// A string after é on line 1; a 4-byte emoji inside a string, then a number, a unit and a comment.
const TEXT = '@source "café.png":\n    @save "日本😀.png" 10px # é\n';
// An error AFTER a non-ASCII token on the same line: the unterminated string at byte column 21.
const BROKEN = '@source "café.png" "b.png:\n    @crop 10%\n';

const withHost = async (body, settings = {}) => {
  const { vscode, calls } = makeVscode({ settings });
  const host = installVscodeStub(vscode);
  try { return await body({ calls, host, vscode }); } finally { host.restore(); }
};
const slice = (text, { start, end }, line) => sourceLines(text)[line].slice(start, end);

test('a byte column becomes the UTF-16 index of the same character', () => {
  const line = '@x "日本" 😀 é';
  assert.equal(unitColumn(line, 1), 0);
  assert.equal(unitColumn(line, 4), 3, 'ASCII before it: bytes and units agree');
  assert.equal(unitColumn(line, 13), 8, 'after two 3-byte characters, 4 bytes fewer');
  assert.equal(unitColumn(line, 18), 11, 'after a 4-byte emoji, two more units than bytes');
  assert.equal(unitColumn('ab', 6), 5, 'past the end of the line a byte is one unit');
});

test('every token of a non-ASCII buffer slices back to its own text', () => {
  const lines = sourceLines(TEXT);
  const tokens = parseScript(TEXT).tokens.filter((t) => t.text !== '\n');
  assert.ok(tokens.some((t) => t.text === '"日本😀.png"'));
  for (const token of tokens) {
    const span = unitSpan(lines, token);
    assert.equal(lines[span.line].slice(span.start, span.end), token.text, `${token.kind} ${token.text}`);
  }
});

test('the in-process squiggle lands on the unterminated string after "café.png"', async () => {
  await withHost(async ({ calls, host }) => {
    const diagnostics = await host.import('diagnostics.js');
    diagnostics.register(makeContext());
    await calls.events.open[0](makeDocument({ text: BROKEN }));
    const [list] = [...calls.collections[0].entries.values()];
    const found = list.find((d) => d.code === 'E_UNTERMINATED_STRING');
    assert.deepEqual([found.range.start.line, found.range.start.character, found.range.end.character], [0, 19, 26]);
    assert.equal(slice(BROKEN, { start: 19, end: 26 }, 0), '"b.png:');
  }, { 'stencil.cliPath': '/nowhere/stencil' });
});

test('a CLI --script-check column is a byte column too, and covers the whole character', async () => {
  await withHost(async ({ host }) => {
    const diagnostics = await host.import('diagnostics.js');
    const lines = sourceLines(BROKEN);
    const [onE, after] = diagnostics.parseCheckOutput([
      'demo.stc:1:13: error: on the é [E_X]',
      'demo.stc:1:21: error: unterminated string [E_UNTERMINATED_STRING]',
    ].join('\n'));
    const e = diagnostics.toDiagnostic(onE, lines).range;
    assert.equal(lines[0].slice(e.start.character, e.end.character), 'é', 'one byte in, both of é out');
    const quote = diagnostics.toDiagnostic(after, lines).range;
    assert.equal(lines[0].slice(quote.start.character, quote.end.character), '"');
  });
});

test('semantic token rows start and end on the right characters', async () => {
  await withHost(async ({ host }) => {
    const tokens = await host.import('semanticTokens.js');
    const built = await tokens.provider.provideDocumentSemanticTokens(makeDocument({ text: TEXT }));
    const texts = built.rows.map(([line, char, length]) => sourceLines(TEXT)[line].slice(char, char + length));
    const whole = new Set(parseScript(TEXT).tokens.map((t) => t.text));
    for (const text of texts) assert.ok(whole.has(text), `${text} is a whole token`);
    for (const text of ['"café.png"', '"日本😀.png"', 'px', '# é']) assert.ok(texts.includes(text), text);
  });
});

test('the hover finds the unit after "日本😀.png", not the number before it', async () => {
  await withHost(async ({ host }) => {
    const hover = await host.import('hover.js');
    const character = sourceLines(TEXT)[1].indexOf('px');
    const answer = await hover.provider.provideHover(makeDocument({ text: TEXT }), { line: 1, character });
    assert.ok(answer, 'a hover at the p of px');
    assert.match(answer.contents.value, /px/);
  });
});

test('a painted colour covers exactly its token on a non-ASCII line', async () => {
  await withHost(async ({ calls, host }) => {
    const decorations = await host.import('decorations.js');
    const editor = makeEditor(makeDocument({ text: TEXT }));
    calls.editors.push(editor);
    const { paintAll } = decorations.register(makeContext());
    paintAll();
    await new Promise((resolve) => { setImmediate(resolve); });
    const painted = [...editor.painted.values()].flat()
      .map((r) => sourceLines(TEXT)[r.start.line].slice(r.start.character, r.end.character));
    assert.ok(painted.includes('"日本😀.png"'), 'the path, emoji and all');
    const texts = new Set(parseScript(TEXT).tokens.map((t) => t.text));
    for (const text of painted) assert.ok(texts.has(text), `${text} is a whole token`);
  });
});

// The span converter before the per-line index: a walk from the line start for every column.
const oldUnitColumn = (text, col) => {
  const over = col - 1 - utf8Length(text);
  return over > 0 ? text.length + over : unitIndexOfColumn(text, col);
};

test('the indexed spans equal the walk-from-line-start spans over a unicode-rich line', () => {
  const lines = ['@save "café 日本😀.png" 10px # é \ud800 x \udc00 😀😀', ''];
  for (const [i, text] of lines.entries()) {
    for (let col = -1; col <= utf8Length(text) + 4; col += 1) {
      assert.equal(unitColumn(text, col), oldUnitColumn(text, col), `col ${col}`);
      for (const len of [0, 1, 3, 4])
        assert.deepEqual(unitSpan(lines, { line: i + 1, col, len }),
          { line: i, start: oldUnitColumn(text, col), end: Math.max(oldUnitColumn(text, col), oldUnitColumn(text, col + len)) });
    }
  }
});
