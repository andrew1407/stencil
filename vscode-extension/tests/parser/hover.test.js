// The explanation under the caret. The token is found in the same parse the colours use, so
// these drive real buffers rather than hand-built spans.
import test from 'node:test';
import assert from 'node:assert/strict';

import { parseScript } from '../../src/parser/index.js';
import { installVscodeStub, makeContext, makeDocument, makeVscode } from '../helpers/vscodeStub.js';

const withHost = async (body, settings) => {
  const { vscode, calls } = makeVscode({ settings });
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, hover: await host.import('hover.js') });
  } finally {
    host.restore();
  }
};

// The caret on the first character of `word`, wherever it first appears in `text`.
const at = (text, word) => {
  const lines = text.split('\n');
  const line = lines.findIndex((l) => l.includes(word));
  return { line, character: lines[line].indexOf(word) };
};

const hoverOver = (text, word) => withHost(({ hover }) =>
  hover.markdownAt(parseScript(text).tokens, at(text, word)));

test('a directive explains itself, with its signature and an example', async () => {
  const markdown = await hoverOver('@source a.png:\n    @crop 10%\n', '@crop');
  assert.match(markdown, /^\*\*@crop\*\* — Cut the picture down/);
  assert.match(markdown, /```stc\n@crop <inset…> \| x1= x2= y1= y2=/);
  assert.match(markdown, /One value insets all four sides/);
});

test('a value word explains itself too', async () => {
  assert.match(await hoverOver('@source a.png:\n    @filter sepia\n', 'sepia'), /^\*\*sepia\*\* — Warm brown/);
  assert.match(await hoverOver('@use line dashed\n', 'dashed'), /^\*\*dashed\*\* — A dashed stroke\./);
  assert.match(await hoverOver('@source a.png:\n    @crop aspect=3:2\n', 'aspect'),
    /^\*\*aspect\*\* — Crop to a ratio\./);
});

test('a unit explains the unit, not the number it hangs off', async () => {
  const text = '@source a.png:\n    @crop 10%\n';
  assert.match(await hoverOver(text, '%'), /^\*\*%\*\* — Per cent/);
  assert.equal(await hoverOver(text, '10'), '', 'the number itself says nothing');
});

test('a parameter explains the positional scheme', async () => {
  const markdown = await hoverOver('@stencil s:\n    @use line @1\n', '@1');
  assert.match(markdown, /template parameter/);
  assert.match(markdown, /the highest index a template mentions \*\*is\*\* its arity/i);
});

test('the case the user typed does not change the answer', async () => {
  assert.equal(
    await hoverOver('@SOURCE a.png:\n    @FILTER BW\n', '@FILTER'),
    await hoverOver('@source a.png:\n    @filter bw\n', '@filter'),
  );
});

test('a path, a point and empty space have nothing to say', async () => {
  assert.equal(await hoverOver('@source shots/*.png:\n', 'shots/*.png'), '');
  assert.equal(await hoverOver('@source a.png:\n    @rect (1,1) (2,2)\n', '('), '');
  await withHost(({ hover }) => {
    const { tokens } = parseScript('@source a.png:\n');
    // The indent of a line that has none: no token covers it.
    assert.equal(hover.markdownAt(tokens, { line: 0, character: 60 }), '');
  });
});

test('a word the language does not know yields no hover at all', async () => {
  await withHost(async ({ hover }) => {
    const document = makeDocument({ text: '@source a.png:\n    @frobnicate 3\n' });
    assert.equal(await hover.provider.provideHover(document, at('@source a.png:\n    @frobnicate 3\n', '@frobnicate')), undefined);
  });
});

test('the provider wraps the Markdown, with HTML off', async () => {
  await withHost(async ({ hover }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crop 10%\n' });
    const result = await hover.provider.provideHover(document, { line: 1, character: 5 });
    assert.match(result.contents.value, /^\*\*@crop\*\*/);
    assert.equal(result.contents.supportHtml, false, 'the vocabulary is Markdown, not HTML');
  });
});

test('stencil.hover off returns nothing, without a reload', async () => {
  await withHost(async ({ hover }) => {
    const document = makeDocument({ text: '@source a.png:\n    @crop 10%\n' });
    assert.equal(await hover.provider.provideHover(document, { line: 1, character: 5 }), undefined);
  }, { 'stencil.hover': false });
});

test('register hands VS Code the provider for stencil-script', async () => {
  await withHost(({ calls, hover }) => {
    const context = makeContext();
    hover.register(context);
    const [registered] = calls.hoverProviders;
    assert.deepEqual(registered.selector, { language: 'stencil-script' });
    assert.equal(registered.provider, hover.provider);
    assert.equal(context.subscriptions.length, 1);
  });
});
