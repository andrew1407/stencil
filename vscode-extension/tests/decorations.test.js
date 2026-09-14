// An exact colour per family, over the palette the extension paints by default. The setting is
// reduced as a pure function; the painting is then driven once through the stub, because what
// matters is which ranges each colour lands on.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

import { parseScript } from '../src/parser/index.js';
import { installVscodeStub, makeContext, makeDocument, makeEditor, makeVscode } from './helpers/vscodeStub.js';

const require = createRequire(import.meta.url);
const families = require('../src/lib/colorFamilies.js');
const { DEFAULTS, FAMILIES, FAMILY_TYPE, overridesFor } = families;

const LIGHT = 1;
const mapped = (palette) => Object.fromEntries(
  Object.entries(palette).map(([family, color]) => [FAMILY_TYPE[family], color]));

const withHost = (body, colors = {}, themeKind = 2) => {
  const { vscode, calls } = makeVscode({ settings: { 'stencil.colors': colors }, themeKind });
  const host = installVscodeStub(vscode);
  try {
    return body({ calls, decorations: host.require('decorations.js'), vscode });
  } finally {
    host.restore();
  }
};

const colorsOf = (calls) => calls.decorationTypes.map((t) => t.options.color);

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

test('with no setting, the built-in palette paints the families a theme reads as something else', () => {
  assert.deepEqual(overridesFor({}), mapped(DEFAULTS.dark));
  assert.deepEqual(overridesFor(undefined), mapped(DEFAULTS.dark));
  assert.deepEqual(overridesFor({}, { light: true }), mapped(DEFAULTS.light));
  for (const palette of [DEFAULTS.dark, DEFAULTS.light]) {
    assert.deepEqual(Object.keys(palette),
      ['source', 'output', 'unit', 'template', 'filterMode', 'path', 'templateName', 'cropEdge']);
    for (const color of Object.values(palette)) assert.match(color, families.HEX);
  }
});

test('a named family wins over the default, and an empty string hands it back to the theme', () => {
  assert.equal(overridesFor({ filterMode: '#ff8800' }).enumMember, '#ff8800');
  assert.equal(overridesFor({ source: '#abc' }).macro, '#abc');
  assert.ok(!('string' in overridesFor({ path: '' })), 'an empty string leaves the theme in charge');
  assert.ok(!('enumMember' in overridesFor({ filterMode: '  ' })));
});

test('a family with no default takes a colour like any other', () => {
  assert.equal(overridesFor({ comment: '#11223344' }).comment, '#11223344');
  assert.deepEqual(overridesFor({ comment: '' }), mapped(DEFAULTS.dark), 'nothing to hand back');
});

test('an unknown family or an unusable value leaves the defaults standing', () => {
  assert.deepEqual(overridesFor({ nonsense: '#fff' }), mapped(DEFAULTS.dark));
  assert.deepEqual(overridesFor({ source: 'red' }), mapped(DEFAULTS.dark), 'a name is not a hex');
  assert.deepEqual(overridesFor({ source: 42 }), mapped(DEFAULTS.dark));
});

test('no setting still builds one decoration type per built-in family', () => {
  withHost(({ calls, decorations }) => {
    decorations.register(makeContext());
    assert.deepEqual(colorsOf(calls).sort(), Object.values(mapped(DEFAULTS.dark)).sort());
  });
});

test('a light theme builds the same families in the light palette', () => {
  withHost(({ calls, decorations }) => {
    decorations.register(makeContext());
    assert.deepEqual(colorsOf(calls).sort(), Object.values(mapped(DEFAULTS.light)).sort());
  }, {}, LIGHT);
});

test('each overridden family becomes one decoration type, in its exact colour', () => {
  withHost(({ calls, decorations }) => {
    decorations.register(makeContext());
    const painted = overridesFor({ filterMode: '#ff8800', source: '#00ff00' });
    assert.equal(painted.macro, '#00ff00', 'the override replaced the default');
    assert.equal(painted.enumMember, '#ff8800');
    assert.deepEqual(colorsOf(calls).sort(), Object.values(painted).sort());
    assert.equal(calls.decorationTypes.length, 8, 'both overrides replaced a default, adding none');
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
    const { paintAll, typesFor } = decorations.register(makeContext());
    paintAll();
    await new Promise((resolve) => { setImmediate(resolve); });
    const types = typesFor();
    assert.equal(types.get('enumMember').options.color, '#ff8800');
    assert.equal(stc.painted.get(types.get('enumMember')).length, 1, 'sepia is the only mode');
    assert.equal(stc.painted.get(types.get('string')).length, 1, 'a.png is the only path');
    assert.deepEqual(stc.painted.get(types.get('type')), [], 'the buffer defines no template');
    assert.equal(other.painted.size, 0, 'a plaintext editor is left alone');
    assert.equal(vscode.window.visibleTextEditors.length, 2);
  }, { filterMode: '#ff8800' });
});

test('changing the setting rebuilds the types, with no reload', async () => {
  await withHost(async ({ calls, decorations, vscode }) => {
    decorations.register(makeContext());
    const built = calls.decorationTypes.length;
    await vscode.workspace.getConfiguration().update('stencil.colors', { source: '#00ff00' });
    calls.events.config[0]({ affectsConfiguration: () => true });
    assert.ok(calls.decorationTypes.slice(0, built).every((t) => t.disposed), 'the old set was let go');
    assert.ok(colorsOf(calls).slice(built).includes('#00ff00'));
  }, { filterMode: '#ff8800' });
});

test('switching to a light theme repaints in the light palette', () => {
  withHost(({ calls, decorations, vscode }) => {
    decorations.register(makeContext());
    const built = calls.decorationTypes.length;
    vscode.window.activeColorTheme.kind = LIGHT;
    calls.events.theme[0]({ kind: LIGHT });
    assert.deepEqual(colorsOf(calls).slice(built).sort(), Object.values(mapped(DEFAULTS.light)).sort());
  });
});

test('the types are disposed when the context is, so a reload leaks nothing', () => {
  withHost(({ calls, decorations }) => {
    const context = makeContext();
    decorations.register(context);
    for (const disposable of context.subscriptions) disposable.dispose?.();
    assert.ok(calls.decorationTypes.every((t) => t.disposed), 'every type was disposed');
  }, { source: '#00ff00' });
});
