// The closeProject chord (common/config/hotkeysConfig.json, js/ui/bindings/keys/hotkeyActions.js):
// listed right after clearProject on a free Alt+Shift+W — matched by the physical key, so a Mac's
// Option+Shift+W „ still lands — and it asks the ✕ drop's question, or says nothing is open.
import test from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../../helpers/dom.js';
import HOTKEY_DEFS from '../../../../../common/config/hotkeysConfig.json' with { type: 'json' };

const doc = installDom();
const toasts = [];
doc.register('notify-balloon', createStubElement('div', { notify: (msg, type) => toasts.push([msg, type]) }));
const { matchHotkey } = await import('../../../../js/utils.js');
const { hotkeyActions } = await import('../../../../js/ui/bindings/keys/hotkeyActions.js');

test('closeProject follows clearProject in the registry, on Alt+Shift+W and nothing else\'s', () => {
  const ids = HOTKEY_DEFS.map((d) => d.id);
  assert.equal(ids.indexOf('closeProject'), ids.indexOf('clearProject') + 1);
  const def = HOTKEY_DEFS.find((d) => d.id === 'closeProject');
  assert.equal(def.default, 'Alt+Shift+W');
  assert.equal(def.label, 'Close Current Project');
  const fold = (c) => c.toLowerCase().split('+').sort().join('+');
  assert.equal(HOTKEY_DEFS.filter((d) => fold(d.default) === fold(def.default)).length, 1, 'the chord is free');
  const press = { code: 'KeyW', key: '„', altKey: true, shiftKey: true, ctrlKey: false, metaKey: false };
  assert.ok(matchHotkey(press, def.default), 'Option+Shift+W types „ on a Mac, and still matches');
  assert.ok(!matchHotkey({ ...press, shiftKey: false }, def.default), 'Alt+W stays Clear All Lines');
});

test('the chord asks the close question, and with nothing open only says so', async () => {
  toasts.length = 0;
  const asked = [];
  const app = {
    activeProjectId: null, closed: [],
    storage: { store: { getMeta: (id) => ({ id, name: 'Site survey' }) } },
    confirm: async (message, opts) => { asked.push({ message, opts }); return true; },
    closeProject(id) { app.closed.push(id); },
  };
  const { HK_HANDLERS } = hotkeyActions(app);
  await HK_HANDLERS.closeProject();
  assert.deepEqual(toasts, [['No project is open', 'info']]);
  assert.equal(asked.length, 0);
  app.activeProjectId = 'p7';
  await HK_HANDLERS.closeProject();
  assert.equal(asked.length, 1);
  assert.equal(asked[0].opts.title, 'Close project?');
  assert.match(asked[0].message, /"Site survey".*stays saved in Projects/);
  assert.deepEqual(app.closed, ['p7']);
});
