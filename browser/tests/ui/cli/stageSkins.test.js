// The CLI stage under a skin (js/ui/cli/stage.js with skin/restyle.js): a skin re-dresses what
// was printed before it as the Zig restyle does every row, and a restyled skin wears no accent.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import FX from '../../../../common/config/console/fx.json' with { type: 'json' };
import { installCliDom, rig, drain } from '../../helpers/cliStageRig.js';

beforeEach(installCliDom);
const { publish, EVENTS } = await import('../../../js/eventBus/appBus.js');
const { traitsOf } = await import('../../../js/ui/cli/skin/table.js');

test('a skin re-dresses what was printed before it, as the Zig restyle does every row: hues over the rainbow, pictures under the cat, every colour the matrix\'s', (t) => {
  const { stage, timers } = rig();
  t.after(() => stage.release());
  const host = stage.host;
  const out = host.children[1];
  const prompt = host.children[3].children[0];
  const input = host.children[3].children[1];
  stage.view.print([{ kind: 'section', text: 'Edit' }, { kind: 'row', text: '/crop <spec>  crop it', head: '/crop <spec>', help: 'crop it' }], 'help');
  stage.view.settleNow();
  drain(timers);
  const section = out.children.at(-2), row = out.children.at(-1);
  const head = () => row.children[0];
  assert.equal(head().textContent, '/crop <spec>', 'plain: the head is its letters');
  stage.wearSkin('rainbow');
  assert.ok(host.hasAttribute('data-cli-runs') && host.hasAttribute('data-cli-restyled'), 'the rainbow is the stylesheet\'s: every accent run, the prompt and the input included, wears the hue gradient');
  assert.ok(String(host.style['--cli-hues']).startsWith('repeating-linear-gradient'));
  assert.equal(head().textContent, '/crop <spec>', 'the letters stay under the hues');
  stage.wearSkin('bifrost');
  assert.ok(host.hasAttribute('data-cli-runs'), 'the bifrost runs the same hues, slid a column a frame through --cli-slide');
  stage.wearSkin('meow');
  assert.ok(head().children.length > 0 && head().children.every((c) => c.nodeType === 3 || c.className === 'cli-picture'), 'the cat turns the head typed before it into cats');
  assert.equal(head().children.filter((c) => c.className === 'cli-picture').length, '/crop<spec>'.length);
  assert.equal(section.children.filter((c) => c.className === 'cli-picture').length, 4, 'the section Edit is four cats');
  assert.equal(host.style['--accent'], '#ffd700', 'the rest of the accent — borders, rules, the prompt, the typed word — turns gold');
  assert.equal(prompt.textContent, '>', 'the prompt row keeps its letters (restyleInput)');
  stage.wearSkin('bifrost');
  input.value = '/crop x1=1';
  input.dataset.word = 'cmd';
  input.dispatch('input');
  const mirror = host.children[3].children[2];
  assert.equal(mirror.className, 'cli-input-mirror', 'under the rainbow a mirror carries the typed line over the input');
  assert.equal(mirror.children[0].textContent, '/crop');
  assert.equal(mirror.children[1].nodeValue, ' x1=1');
  assert.equal(mirror.dataset.word, 'cmd');
  stage.wearSkin('matrix');
  assert.equal(host.children[3].children.length, 2, 'the mirror leaves with the hues');
  assert.ok(host.hasAttribute('data-cli-cells'));
  assert.equal(host.style['--accent'], traitsOf('matrix').fg, 'the matrix paints the accent its green');
  assert.equal(head().textContent, '/crop <spec>', 'and the letters are back');
  stage.wearSkin(null);
  assert.equal(host.style['--accent'], undefined);
  assert.ok(!host.hasAttribute('data-cli-restyled'));
});

test('under a restyled skin a new accent lands at once — the skin wears no accent — and the fairy lights still wipe', (t) => {
  const { app, stage, timers } = rig();
  t.after(() => stage.release());
  stage.wearSkin('meow');
  timers.splice(0);
  app.accents.setAccent('brown');
  publish(EVENTS.accentChanged, {});
  assert.equal(timers.filter(([, ms]) => ms === FX.wipe.stepMs).length, 0, 'no wipe step under the cat');
  stage.wearSkin('fairylight');
  timers.splice(0);
  app.accents.setCustomAccent('#46e164');
  publish(EVENTS.accentChanged, {});
  assert.equal(timers.length, 1, 'the lights wipe their bulb in');
  drain(timers);
});
