// config/console/commands.json is the asset: every row is well formed, names and aliases are
// unique across both surfaces, every section is declared, and the console window's row in
// uiStrings points at the markup it opens.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import TABLE from '../../../common/config/console/commands.json' with { type: 'json' };
import UI_STRINGS from '../../../common/config/uiStrings.json' with { type: 'json' };
import HOTKEYS from '../../../common/config/hotkeysConfig.json' with { type: 'json' };
import { layout } from '../../js/ui/layout.js';

test('every row is well formed, named once, in a declared section, on a known surface', () => {
  const seen = new Set();
  for (const c of TABLE.commands) {
    assert.match(c.name, /^[a-z][a-z-]*$/, c.name);
    assert.ok(TABLE.sections.includes(c.section), `${c.name}: ${c.section}`);
    assert.ok(c.help.length > 8, `${c.name} says what it does`);
    assert.ok(Array.isArray(c.aliases) && typeof c.arg === 'string');
    assert.ok(c.surfaces.length && c.surfaces.every((s) => ['browser', 'desktop'].includes(s)), c.name);
    for (const n of [c.name, ...c.aliases]) { assert.equal(seen.has(n), false, `${n} is named twice`); seen.add(n); }
  }
  assert.deepEqual(Object.keys(TABLE.cliMode).sort(), ['holdMs', 'offCommand', 'typedWord']);
  assert.ok(TABLE.commands.some((c) => c.name === TABLE.cliMode.offCommand && c.cliMode));
});

test('the console window row names its overlay, its opener and its hotkey, all composed', () => {
  const row = UI_STRINGS.windows.find((w) => w.key === 'console');
  assert.deepEqual([row.overlay, row.opener, row.hotkey], ['console-overlay', 'console-btn', 'openConsole']);
  const markup = layout();
  assert.ok(markup.includes('id="console-overlay"') && markup.includes('id="console-btn"'));
  assert.ok(HOTKEYS.some((h) => h.id === 'openConsole' && h.default === 'Alt+Shift+T'));
  assert.ok(markup.indexOf('id="console-btn"') < markup.indexOf('id="script-btn"'), 'the console icon leads the Data section');
});
