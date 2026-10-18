// The /help layout (js/console/shell/help.js): sections in table order, each row's command in a
// gutter, and the CLI-mode rows only when asked for.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { helpLines, HELP_GUTTER } from '../../../js/console/shell/help.js';

test('help lists every section with its rows, the command padded into the gutter', () => {
  const lines = helpLines();
  assert.equal(lines[0].kind, 'plain');
  const sections = lines.filter((l) => l.kind === 'section').map((l) => l.text);
  assert.deepEqual(sections, ['Image', 'Edit', 'Save', 'Connections', 'Apps', 'Assistant', 'System']);
  const crop = lines.find((l) => l.head === '/crop <spec> [album]');
  assert.ok(crop, 'the crop row');
  assert.equal(crop.text.indexOf(crop.help), HELP_GUTTER);
  assert.ok(!lines.some((l) => l.head === '/clioff'), 'no CLI-mode row in the window');
});

test('in CLI mode the help carries its own rows and the way out', () => {
  const lines = helpLines({ cliMode: true });
  assert.ok(lines.some((l) => l.head === '/clioff'));
  assert.ok(lines.some((l) => l.head === '/ls'));
  assert.match(lines.at(-1).text, /\/clioff/);
});

test('a head wider than the gutter keeps one space before its help, as the Zig pins lay it out', () => {
  const long = helpLines().filter((l) => l.kind === 'row' && l.head.length >= HELP_GUTTER);
  assert.ok(long.length >= 1, 'the table has a head past the gutter');
  for (const l of long) assert.equal(l.text, `${l.head} ${l.help}`);
});
