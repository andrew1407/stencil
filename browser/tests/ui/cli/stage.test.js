// The CLI stage (js/ui/cli/stage.js): the fairy lights recolour the banner without a press and
// hand the accent back on release, a custom one too; the wipe lights the wordmark's letters in
// order with the spaces skipped; nothing of the stage outlives release().
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import FX from '../../../../common/config/console/fx.json' with { type: 'json' };
import { installCliDom, rig, presses, drain, body } from '../../helpers/cliStageRig.js';

beforeEach(installCliDom);
const { publish, EVENTS } = await import('../../../js/eventBus/appBus.js');
const { accentHex, ACCENTS, DEFAULT_ACCENT } = await import('../../../js/core/settings/accents.js');
const { SKIN_NAMES, traitsOf } = await import('../../../js/ui/cli/skin/table.js');

test('every new accent wipes over the banner, a fairy-light bulb too, with no press frame; the lights give a custom accent back', (t) => {
  const { app, stage, timers, banner } = rig();
  t.after(() => stage.release());
  const fullRows = banner.children.length;
  timers.splice(0);   // the prompt's focus tick
  app.accents.setAccent('brown');
  publish(EVENTS.accentChanged, {});
  assert.equal(presses(timers), 0, 'no press frame on a plain accent change');
  assert.equal(timers.length, 1, 'the wipe is scheduled');
  assert.equal(banner.children.length, fullRows, 'the banner stays full while it wipes');
  let stepped = false;
  while (timers.length && !stepped) { const [fn, ms] = timers.shift(); stepped = ms === FX.wipe.stepMs; fn(); }
  assert.ok(stepped, 'and jumps at the wipe step');
  drain(timers);
  app.accents.setCustomAccent('#123456');
  publish(EVENTS.accentChanged, {});
  drain(timers);
  stage.wearSkin('fairylight');
  app.accents.setCustomAccent('#46e164');
  publish(EVENTS.accentChanged, {});
  assert.equal(presses(timers), 0, 'a bulb is a wipe, not a press');
  assert.equal(timers.length, 1);
  drain(timers);
  stage.wearSkin(null);
  assert.deepEqual(app.calls.at(-1), ['setCustomAccent', '#123456'], 'the custom accent comes back when the lights go out');
});

test('release gives the accent back after the subscriptions are gone, so nothing wipes a detached banner', (t) => {
  const { app, stage, timers } = rig();
  t.after(() => stage.release());
  stage.wearSkin('fairylight');
  const before = presses(timers);
  stage.release();
  assert.deepEqual(app.calls.at(-1), ['setAccent', 'violet']);
  publish(EVENTS.accentChanged, {});
  assert.equal(presses(timers), before, 'released: no press is scheduled');
  drain(timers);
  assert.ok(!body.children.includes(stage.host));
});

test('the wipe lights the wordmark letter by letter, spaces skipped, over the full banner', (t) => {
  const { app, stage, timers, letters, banner } = rig();
  t.after(() => stage.release());
  banner.dispatch('pointerdown', { button: 0 });
  timers.splice(timers.findIndex(([, ms]) => ms === FX.press.ms), 1)[0][0]();
  banner.dispatch('pointerleave');
  app.accents.setAccent('brown');
  const next = accentHex('brown');
  publish(EVENTS.accentChanged, {});
  assert.equal(letters().length, 'S T E N C I L'.length, 'the wipe runs over the full banner, wordmark included');
  const lit = () => letters().filter((l) => l.style.color === next).map((l) => l.textContent);
  const seen = [];
  while (timers.length) {
    timers.shift()[0]();
    seen.push(lit().join(''));
  }
  for (const ch of 'STENCI') assert.ok(seen.includes(ch), `the plain wordmark lights one letter at a time (press.zig drawWordmark): ${seen.join('|')}`);
  assert.ok(seen.every((s) => s.length <= 1), 'never two at once, never a space');
  assert.equal(lit().length, 0, 'settled: the banner is repainted whole, no letter singled out');
});

test('the logo is the Zig one: the press shows the press frame, a click advances the accent after the double-click window, a double-click picks a vivid colour', (t) => {
  const { app, stage, timers, banner } = rig();
  t.after(() => stage.release());
  const fullRows = banner.children.length;
  const marks = () => banner.querySelectorAll('.cli-mark').length;
  const fullMarks = marks();
  const keys = ACCENTS.map((a) => a.key);
  const next = keys[(keys.indexOf(app.accent) + 1) % keys.length];
  banner.dispatch('pointerdown', { button: 0 });
  assert.equal(banner.children.length, fullRows, 'the press frame keeps the logo box, as press.zig does');
  assert.ok(marks() < fullMarks, 'with the smaller icon in it');
  assert.ok(banner.querySelectorAll('.cli-letter').length > 0, 'and the wordmark stays');
  assert.equal(presses(timers), 1);
  timers.splice(timers.findIndex(([, ms]) => ms === FX.press.ms), 1)[0][0]();
  assert.ok(marks() < fullMarks, 'the frame stays as long as the button is down');
  banner.dispatch('pointerleave');
  assert.equal(marks(), fullMarks, 'the banner is whole again once it is let go');
  banner.dispatch('pointerdown', { button: 0 }); banner.dispatch('pointerup', { button: 0 });
  assert.notDeepEqual(app.calls.at(-1), ['setAccent', next], 'the accent waits out the double-click window');
  const wait = timers.splice(timers.findIndex(([, ms]) => ms === 250), 1)[0];
  wait[0]();
  assert.deepEqual(app.calls.at(-1), ['setAccent', next]);
  drain(timers);
  app.accents.setCustomAccent('#123456');
  banner.dispatch('pointerdown', { button: 0 }); banner.dispatch('pointerup', { button: 0 });
  timers.splice(timers.findIndex(([, ms]) => ms === 250), 1)[0][0]();
  assert.deepEqual(app.calls.at(-1), ['setAccent', DEFAULT_ACCENT], 'a custom accent goes back to the default');
  banner.dispatch('pointerdown', { button: 0 }); banner.dispatch('pointerup', { button: 0 });
  banner.dispatch('pointerdown', { button: 0 }); banner.dispatch('pointerup', { button: 0 });   // within the window: a double-click
  const [verb, hex] = app.calls.at(-1);
  assert.equal(verb, 'setCustomAccent');
  assert.match(hex, /^#[0-9a-f]{6}$/, 'a double-click: a random vivid custom colour (hooks.zig logoCustom)');
  assert.equal(banner.querySelectorAll('input').length, 0, 'no colour picker on the CLI logo');
  stage.wearSkin('matrix');
  banner.dispatch('pointerdown', { button: 0 }); banner.dispatch('pointerup', { button: 0 });
  timers.splice(timers.findIndex(([, ms]) => ms === 250), 1)[0][0]();
  assert.equal(stage.skin(), null, 'under a skin, a click takes the skin off and changes nothing else');
});

test('every secret skin goes on and off the live stage, the picture skins with their pictures', (t) => {
  const { stage, banner } = rig();
  t.after(() => stage.release());
  for (const name of SKIN_NAMES) {
    stage.wearSkin(name);
    assert.equal(stage.skin(), name);
    if (traitsOf(name).replacesLetters && name !== 'pie') assert.ok(banner.querySelectorAll('.cli-picture').length > 0, `${name} draws its pictures`);
    stage.wearSkin(null);
    assert.equal(stage.skin(), null);
  }
});

test('a press during a wipe settles it first, as queued input does in the Zig console', (t) => {
  const { app, stage, timers, banner } = rig();
  t.after(() => stage.release());
  timers.splice(0);
  app.accents.setAccent('brown');
  publish(EVENTS.accentChanged, {});
  timers.shift()[0]();   // the wipe's first wash
  banner.dispatch('pointerdown', { button: 0 });
  timers.splice(timers.findIndex(([, ms]) => ms === FX.press.ms), 1)[0][0]();
  banner.dispatch('pointerleave');
  const next = accentHex('brown');
  const cells = [...banner.querySelectorAll('.cli-accent')];
  assert.ok(cells.length > 0 && cells.every((c) => c.style.color === next), 'settled: every border cell wears the new accent');
  assert.ok(banner.querySelectorAll('.cli-mark').length > 0, 'and the full banner is back');
});

