// What the list offers at a caret. The context table is asserted as a pure function; the
// provider is then driven once through the stub to prove it reads the right line.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript } from '../src/parser/index.js';
import { installVscodeStub, makeContext, makeDocument, makeVscode } from './helpers/vscodeStub.js';

import { contextFor } from '../src/lib/vocab/completionContext.js';

const withHost = async (body, settings) => {
  const { vscode, calls } = makeVscode({ settings });
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, completion: await host.import('completion.js') });
  } finally {
    host.restore();
  }
};

const labels = (linePrefix, text = '') => withHost(({ completion }) => {
  const { tokens } = parseScript(text);
  return completion.itemsFor(linePrefix, tokens).map((i) => i.label);
});

test('an empty statement offers the directives, and only those', async () => {
  assert.deepEqual(contextFor('    ').groups, ['directive']);
  const offered = await labels('    ');
  assert.ok(offered.includes('@source') && offered.includes('@undo'), offered.join(' '));
  assert.equal(offered.length, 12, 'the twelve directives');
  for (const label of offered) assert.match(label, /^@[a-z]+$/);
});

test('a half-typed directive still offers directives — VS Code does the filtering', async () => {
  assert.deepEqual(contextFor('    @fil').groups, ['directive']);
  assert.ok((await labels('    @fil')).includes('@filter'));
});

test('@filter offers its five modes, then the colour names under them', async () => {
  const offered = await labels('    @filter ');
  assert.deepEqual(offered.slice(0, 5), ['bw', 'sepia', 'invert', 'contour', 'none']);
  assert.ok(offered.includes('red') && offered.includes('transparent'), 'colours too');
});

test('@use branches on its sub-keyword', async () => {
  assert.deepEqual((await labels('    @use ')).slice(0, 2), ['line', 'stencil']);
  const style = await labels('    @use line ');
  for (const word of ['solid', 'dashed', 'dotted', 'fill', 'point']) {
    assert.ok(style.includes(word), `${word} is missing`);
  }
  assert.ok(style.includes('px'), 'a thickness takes a unit');
});

test('@crop offers its named edges', async () => {
  assert.deepEqual((await labels('    @crop ')).slice(0, 5), ['x1', 'x2', 'y1', 'y2', 'aspect']);
});

test('@layout offers its two modes, but only after a path', async () => {
  assert.deepEqual(await labels('    @layout '), []);
  assert.deepEqual(await labels('    @layout grid.json '), ['combine', 'replace']);
});

test('@use stencil offers the templates this file defines', async () => {
  const text = '@stencil box frame:\n    @filter bw\n\n@stencil tint:\n    @filter sepia\n';
  assert.deepEqual(await labels('    @use stencil ', text), ['box frame', 'tint']);
  assert.deepEqual(await labels('    @use stencil ', ''), [], 'a file with no template offers none');
});

test('a length being typed offers whole replacements, number and unit together', async () => {
  assert.equal(contextFor('    @crop 10').numberStem, '10');
  assert.deepEqual(await labels('    @crop 10'), ['10px', '10cm', '10mm', '10in', '10%']);
  assert.deepEqual(await labels('    @crop -2.5p'), ['-2.5px', '-2.5cm', '-2.5mm', '-2.5in', '-2.5%']);
});

test('a directive that names a path offers nothing — the list must not guess a filename', async () => {
  for (const prefix of ['@source ', '    @save ', '    @frame ', '    @undo ']) {
    assert.deepEqual(await labels(prefix), [], prefix);
  }
});

test('a parameter does not shadow the directive it sits under', () => {
  assert.deepEqual(contextFor('    @use line @1, ').groups, ['style', 'color', 'unit']);
});

test('every item carries the documentation the hover would show', async () => {
  await withHost(({ completion }) => {
    const items = completion.itemsFor('    @filter ', []);
    const bw = items.find((i) => i.label === 'bw');
    assert.match(bw.documentation.value, /\*\*bw\*\* — Black and white\./);
    const red = items.find((i) => i.label === 'red');
    assert.equal(red.detail, '#ff0000', 'a colour item shows its hex');
  });
});

test('stencil.completion off returns nothing, without a reload', async () => {
  await withHost(async ({ completion }) => {
    const document = makeDocument({ text: '@source a.png:\n    @filter \n' });
    const items = await completion.provider.provideCompletionItems(document, { line: 1, character: 12 });
    assert.deepEqual(items, []);
  }, { 'stencil.completion': false });
});

test('register hands VS Code the provider for stencil-script, triggered on @', async () => {
  await withHost(({ calls, completion }) => {
    const context = makeContext();
    completion.register(context);
    const [registered] = calls.completionProviders;
    assert.deepEqual(registered.selector, { language: 'stencil-script' });
    assert.deepEqual(registered.triggers, ['@', '%']);
    assert.equal(context.subscriptions.length, 1);
  });
});

test('the provider reads the caret line out of the document', async () => {
  await withHost(async ({ completion }) => {
    const document = makeDocument({ text: '@source a.png:\n    @filter \n' });
    const items = await completion.provider.provideCompletionItems(document, { line: 1, character: 12 });
    assert.deepEqual(items.slice(0, 5).map((i) => i.label), ['bw', 'sepia', 'invert', 'contour', 'none']);
  });
});

test('the caret line is read through lineAt, at every legal position in the buffer', async () => {
  await withHost(async ({ completion }) => {
    const text = '@source a.png:\r\n    @filter bw\n\n@save out.png\n';
    const document = makeDocument({ text });
    // Every line the document has, including the empty one and the trailing one.
    for (let line = 0; line < text.split(/\r?\n/).length; line += 1) {
      const items = await completion.provider.provideCompletionItems(document, { line, character: 0 });
      assert.ok(Array.isArray(items), `line ${line} threw or answered nothing`);
    }
    await assert.rejects(
      () => completion.provider.provideCompletionItems(document, { line: 99, character: 0 }),
      RangeError, 'the stub is faithful: an out-of-range line is an error, not an empty string');
  });
});
