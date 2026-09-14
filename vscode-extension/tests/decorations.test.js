// An exact colour per family. The setting is reduced as a pure function; the painting is then
// driven once through the stub, because what matters is which ranges each colour lands on.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

import { parseScript } from '../src/parser/index.js';
import { installVscodeStub, makeContext, makeDocument, makeEditor, makeVscode } from './helpers/vscodeStub.js';

const require = createRequire(import.meta.url);
const { FAMILIES, FAMILY_TYPE, overridesFor } = require('../src/lib/colorFamilies.js');

const withHost = (body, colors = {}) => {
  const { vscode, calls } = makeVscode({ settings: { 'stencil.colors': colors } });
  const host = installVscodeStub(vscode);
  try {
    return body({ calls, decorations: host.require('decorations.js'), vscode });
  } finally {
    host.restore();
  }
};

test('every family names a type that is actually in the legend', () => {
  withHost(({ decorations }) => {
    const { TOKEN_TYPES } = require('../src/semanticTokens.js');
    assert.equal(FAMILIES.length, TOKEN_TYPES.length, 'one family per legend entry');
    for (const family of FAMILIES) {
      assert.ok(TOKEN_TYPES.includes(FAMILY_TYPE[family]), `${family} maps outside the legend`);
    }
    assert.ok(decorations.rangesFor, 'the module loaded under the stub');
  });
});

test('only a real family with a real hex colour survives', () => {
  assert.deepEqual(overridesFor({ filterMode: '#ff8800' }), { enumMember: '#ff8800' });
  assert.deepEqual(overridesFor({ source: '#abc', comment: '#11223344' }),
    { macro: '#abc', comment: '#11223344' });
  assert.deepEqual(overridesFor({ nonsense: '#fff' }), {}, 'an unknown family is dropped');
  assert.deepEqual(overridesFor({ source: 'red' }), {}, 'a colour name is not a hex colour');
  assert.deepEqual(overridesFor({ source: '' }), {}, 'empty means: leave it to the theme');
  assert.deepEqual(overridesFor(undefined), {});
});

test('no setting means no decoration type at all — the theme is left alone', () => {
  withHost(({ calls, decorations }) => {
    decorations.register(makeContext());
    assert.deepEqual(calls.decorationTypes, []);
  });
});

test('each overridden family becomes one decoration type, in its exact colour', () => {
  withHost(({ calls, decorations }) => {
    decorations.register(makeContext());
    assert.deepEqual(calls.decorationTypes.map((t) => t.options.color).sort(),
      ['#00ff00', '#ff8800']);
  }, { filterMode: '#ff8800', source: '#00ff00' });
});

test('rangesFor puts a token under the type the classifier gave it', () => {
  withHost(({ decorations }) => {
    const text = '@source a.png:\n    @filter sepia\n';
    const ranges = decorations.rangesFor(parseScript(text).tokens, ['macro', 'enumMember']);
    const [source] = ranges.get('macro');
    assert.deepEqual([source.start.line, source.start.character, source.end.character], [0, 0, 7]);
    const [mode] = ranges.get('enumMember');
    assert.deepEqual([mode.start.line, mode.start.character, mode.end.character], [1, 12, 17]);
  });
});

test('a type with no tokens is painted with an empty list, clearing a stale colour', () => {
  withHost(({ decorations }) => {
    const ranges = decorations.rangesFor(parseScript('@source a.png:\n').tokens, ['enumMember']);
    assert.deepEqual(ranges.get('enumMember'), []);
  });
});

test('painting reaches every visible .stc editor and skips the rest', async () => {
  await withHost(async ({ calls, decorations, vscode }) => {
    const stc = makeEditor(makeDocument({ text: '@source a.png:\n    @filter sepia\n' }));
    const other = makeEditor(makeDocument({ text: 'hello', languageId: 'plaintext' }));
    calls.editors.push(stc, other);
    const { paintAll } = decorations.register(makeContext());
    paintAll();
    await new Promise((resolve) => { setImmediate(resolve); });
    assert.equal(stc.painted.size, 1, 'the one overridden family was painted');
    const [[type, list]] = [...stc.painted];
    assert.equal(type.options.color, '#ff8800');
    assert.equal(list.length, 1, 'sepia is the only filter mode in the buffer');
    assert.equal(other.painted.size, 0, 'a plaintext editor is left alone');
    assert.equal(vscode.window.visibleTextEditors.length, 2);
  }, { filterMode: '#ff8800' });
});

test('changing the setting rebuilds the types, with no reload', async () => {
  await withHost(async ({ calls, decorations, vscode }) => {
    decorations.register(makeContext());
    assert.deepEqual(calls.decorationTypes.map((t) => t.options.color), ['#ff8800']);
    await vscode.workspace.getConfiguration().update('stencil.colors', { source: '#00ff00' });
    calls.events.config[0]({ affectsConfiguration: () => true });
    assert.ok(calls.decorationTypes[0].disposed, 'the colour it replaced was let go');
    assert.deepEqual(calls.decorationTypes.map((t) => t.options.color), ['#ff8800', '#00ff00']);
  }, { filterMode: '#ff8800' });
});

test('the types are disposed when the context is, so a reload leaks nothing', () => {
  withHost(({ calls, decorations }) => {
    const context = makeContext();
    decorations.register(context);
    for (const disposable of context.subscriptions) disposable.dispose?.();
    assert.ok(calls.decorationTypes.every((t) => t.disposed), 'every type was disposed');
  }, { source: '#00ff00' });
});
