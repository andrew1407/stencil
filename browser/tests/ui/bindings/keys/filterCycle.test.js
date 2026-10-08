// The image-filter cycle both ways: cycleFilter (Alt+B) steps forward and cycleFilterPrev
// (Alt+Shift+B) back, each wrapping and each one pick through setImageFilter, as the toolbar's.
// The keydown loop matches the physical key, so macOS's Option+Shift+B, which types "ı", still
// lands. Desktop twin: ActionsBuilderWiring.cpp, pinned in tests/canvas/MainWindow.canvasFilter.gui.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';
import HOTKEY_DEFS from '../../../../../common/config/hotkeysConfig.json' with { type: 'json' };
import { installDom } from '../../../helpers/dom.js';

const doc = installDom({}, { window: { addEventListener() {} } });
const { hotkeyActions } = await import('../../../../js/ui/bindings/keys/hotkeyActions.js');
const { wireKeyboard } = await import('../../../../js/ui/bindings/keys/keyboard.js');

const ORDER = ['none', 'bw', 'sepia', 'invert', 'contour', 'custom'];
const filterApp = (imageFilter) => {
  const picks = [];
  const app = { imageFilter, settings: { setImageFilter: (f) => { picks.push(f); app.imageFilter = f; } } };
  return { app, picks };
};

test('cycleFilterPrev follows cycleFilter in the registry, on its own Alt+Shift+B', () => {
  const ids = HOTKEY_DEFS.map((d) => d.id);
  assert.equal(ids.indexOf('cycleFilterPrev'), ids.indexOf('cycleFilter') + 1);
  const def = HOTKEY_DEFS.find((d) => d.id === 'cycleFilterPrev');
  assert.deepEqual([def.label, def.default], ['Previous Image Filter', 'Alt+Shift+B']);
  assert.equal(HOTKEY_DEFS.filter((d) => d.default === def.default).length, 1, 'the default chord is unique');
});

test('forward and back each walk the whole cycle, wrapping, one pick per step', () => {
  const { app, picks } = filterApp('none');
  const { HK_HANDLERS } = hotkeyActions(app);
  for (let i = 0; i < ORDER.length; i++) HK_HANDLERS.cycleFilter();
  assert.deepEqual(picks, [...ORDER.slice(1), 'none']);
  picks.length = 0;
  for (let i = 0; i < ORDER.length; i++) HK_HANDLERS.cycleFilterPrev();
  assert.deepEqual(picks, ['custom', 'contour', 'invert', 'sepia', 'bw', 'none']);
});

test('the keydown loop sends Option+Shift+B back and Option+B forward, by the physical key', () => {
  const { app, picks } = filterApp('sepia');
  wireKeyboard(app);
  const press = (key, shiftKey) => {
    let claimed = false;
    doc.dispatch('keydown', { key, code: 'KeyB', altKey: true, shiftKey, ctrlKey: false, metaKey: false,
      preventDefault: () => { claimed = true; } });
    return claimed;
  };
  assert.ok(press('ı', true), 'macOS types "ı" for Option+Shift+B; the chord is still the hotkey');
  assert.deepEqual(picks, ['bw']);
  assert.ok(press('∫', false));
  assert.deepEqual(picks, ['bw', 'sepia']);
});
