// The console's handlers (js/console/shell/handlers*.js) over the facade rig: each command
// reaches the facade member a click would, and a bad argument throws the message to show.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, lastCall, called, withProjects } from '../../helpers/stencilApiRig.js';
import { createHandlers } from '../../../js/console/shell/handlers.js';
import { parseBlank } from '../../../js/console/shell/handlersEdit.js';

const rig = (over = {}) => {
  const app = { ...withProjects(), ...over };
  app.accents.themeMode = 'system';
  app.accents.setThemeMode = (m) => { app.calls.push(['setThemeMode', m]); app.accents.themeMode = m; };
  app.history = { canUndo: () => app.calls.filter((c) => c[0] === 'undo').length < 2 };
  const stencil = createStencil(app);
  const picked = [];
  const host = { pickFile: () => picked.push(1), clear() {}, close() {}, cliMode: () => false, secret: () => false };
  return { app, stencil, host, picked, h: createHandlers({ app, stencil }) };
};

test('every browser row of the table has a handler and every handler a row', async () => {
  const { COMMANDS } = await import('../../../js/console/shell/table.js');
  const { h } = rig();
  const windowRows = COMMANDS.filter((c) => !c.cliMode).map((c) => c.name).sort();
  assert.deepEqual(Object.keys(h).sort(), windowRows);
});

test('rotate turns a quarter at a time, a full turn changes nothing, a non-integer throws', () => {
  const { app, h } = rig({ image: { width: 4, height: 2 } });
  assert.equal(h.rotate('1'), 'rotated 90°');
  assert.deepEqual(lastCall(app, 'rotateImage'), ['rotateImage', 1]);
  assert.equal(h.rotate('-1'), 'rotated -90°');
  assert.deepEqual(lastCall(app, 'rotateImage'), ['rotateImage', -1], '-1 is one turn left, one history step');
  assert.equal(h.rotate('3'), 'rotated 270°');
  assert.equal(called(app, 'rotateImage').length, 3, 'three right is one left');
  assert.equal(h.rotate('4'), 'no change (a full turn)');
  assert.throws(() => h.rotate('x'), /whole number/);
  assert.throws(() => rig().h.rotate('1'), /no image loaded/);
});

test('theme: dark/light/auto set the mode, anything else the accent, and a bad accent throws', () => {
  const { app, h, stencil } = rig();
  assert.equal(h.theme('dark'), 'theme: dark');
  assert.deepEqual(lastCall(app, 'setThemeMode'), ['setThemeMode', 'dark']);
  assert.equal(h.theme('auto'), 'theme: system');
  h.theme('sky');
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'sky']);
  h.theme('#ff5623');
  assert.deepEqual(lastCall(app, 'setCustomAccent'), ['setCustomAccent', '#ff5623']);
  assert.throws(() => h.theme('nope'), /Unknown theme/);
  assert.match(h.theme('')[1], new RegExp(stencil.mainThemes[0]));
});

test('filter, color, crop and flip reach their setters; upload with no URL asks the host for a file', async () => {
  const { app, h, host, picked } = rig({ image: { width: 4, height: 2 }, originalImage: { width: 4, height: 2 } });
  assert.equal(h.filter('bw'), 'filter: bw');
  assert.deepEqual(lastCall(app, 'setImageFilter'), ['setImageFilter', 'bw']);
  h.filter('#00ff00');
  assert.deepEqual(lastCall(app, 'setFilterColor'), ['setFilterColor', '#00ff00']);
  h.color('#112233');
  assert.deepEqual(lastCall(app, 'setColor'), ['setColor', '#112233']);
  h.flip();
  assert.ok(lastCall(app, 'flipImage'));
  assert.throws(() => h.crop('nonsense'), /crop takes edges/);
  await h.upload('', host);
  assert.equal(picked.length, 1);
});

test('open switches to a saved project by name or id; a missing one throws; projects lists them', async () => {
  const { app, h, host } = rig();
  assert.equal(await h.open('beta', host), 'opened Beta');
  assert.deepEqual(lastCall(app, 'switchToProject'), ['switchToProject', 2]);
  assert.equal(await h.open('1', host), 'opened Alpha');
  await assert.rejects(() => h.open('zeta', host), /no project called 'zeta'/);
  const rows = await h.projects('');
  assert.ok(rows.some((r) => /Alpha/.test(r)) && rows.some((r) => /Beta/.test(r)));
  assert.equal(h.undo(), 'undone');
  assert.match(h.reset(), /reset: \d edit/);
});

test('parseBlank: a format or a pixel pair, then a colour; never both, never junk', () => {
  assert.deepEqual(parseBlank('b5 pink'), { format: 'B5', width: null, height: null, color: 'pink' });
  assert.deepEqual(parseBlank('800 600'), { format: null, width: 800, height: 600, color: '#ffffff' });
  assert.equal(parseBlank('a4 800 600'), null);
  assert.equal(parseBlank('800'), null);
  assert.equal(parseBlank('white extra'), null);
  assert.deepEqual(parseBlank(''), { format: null, width: null, height: null, color: '#ffffff' });
});
