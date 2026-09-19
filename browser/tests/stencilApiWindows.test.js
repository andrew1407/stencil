// stencil.openWindow and the named openers (js/console/stencilApi.js): each window opens
// through its own shell from its toolbar control, whose disabled state gates the route.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from './helpers/dom.js';
import { createStencil, makeApp, called } from './helpers/stencilApiRig.js';

// Each window opens through its own shell (the overlay's __stencilModal), flying out of its toolbar
// control, whose disabled state gates the script route exactly like a click.
const withWindows = (fn, { disabled = {}, missing = [] } = {}) => {
  const original = document.getElementById;
  const shells = {};
  const buttons = {};
  const lookup = (id) => {
    if (missing.includes(id)) return null;
    if (id.endsWith('-overlay')) {
      return shells[id] ||= (() => {
        let open = false;
        const shell = { opens: [], isOpen: () => open,
          open(from) { open = true; shell.opens.push(from); }, close() { open = false; } };
        return { __stencilModal: shell };
      })();
    }
    if (id.endsWith('-btn') || id === 'crop-image') {
      return buttons[id] ||= createStubElement('button', { id, disabled: id in disabled,
        dataset: typeof disabled[id] === 'string' ? { disabledReason: disabled[id] } : {} });
    }
    return original(id);
  };
  document.getElementById = lookup;
  try { fn({ shells, buttons }); } finally { document.getElementById = original; }
};

test('openWindow matches titles loosely and opens through the window shell from its control', () => {
  withWindows(({ shells, buttons }) => {
    const stencil = createStencil(makeApp());
    assert.ok(stencil.windows.includes('Projects') && stencil.windows.includes('Servers'));
    assert.equal(stencil.openedWindow, null);
    assert.equal(stencil.openWindow('projects'), stencil);
    const projects = shells['projects-modal-overlay'].__stencilModal;
    assert.equal(projects.opens[0], buttons['projects-btn'], 'flies out of the toolbar icon');
    assert.equal(stencil.openedWindow, 'Projects');
    stencil.openWindow('Projects');
    assert.equal(projects.opens.length, 1, 'already open: a no-op, not a toggle');
    // Title spellings: case, punctuation and the hotkey id all land.
    stencil.openWindow('visuals & settings');
    assert.ok(shells['visuals-modal-overlay'].__stencilModal.isOpen());
    stencil.openWindow('Open In…');
    assert.ok(shells['open-in-modal-overlay'].__stencilModal.isOpen());
    stencil.openWindow('openHotkeys');
    assert.ok(shells['settings-modal-overlay'].__stencilModal.isOpen());
    assert.throws(() => stencil.openWindow('teleport'), /no window called "teleport"/);
    assert.throws(() => stencil.openWindow(''), /no window called/);
  });
});

test('the named openers route to the same windows; connections is the Servers window', () => {
  withWindows(({ shells, buttons }) => {
    const stencil = createStencil(makeApp());
    const isOpen = (overlay) => shells[overlay].__stencilModal.isOpen();
    stencil.openServersWindow();
    assert.ok(isOpen('connect-modal-overlay'));
    stencil.openConnectionsWindow();
    assert.equal(shells['connect-modal-overlay'].__stencilModal.opens.length, 1);
    stencil.openAssistantSettingsWindow();
    assert.equal(shells['chat-settings-overlay'].__stencilModal.opens[0], buttons['chat-settings-btn'],
      'the AI settings fly from the chat gear, like the … menu route');
    for (const [fn, overlay] of [
      ['openProjectsWindow', 'projects-modal-overlay'], ['openLinksWindow', 'links-modal-overlay'],
      ['openDescriptionWindow', 'description-overlay'], ['openKeywordsWindow', 'keywords-overlay'],
      ['openShortcutsWindow', 'settings-modal-overlay'], ['openVisualsWindow', 'visuals-modal-overlay'],
      ['openHelpWindow', 'info-modal-overlay'], ['openImageWindow', 'open-image-modal-overlay'],
      ['openCropWindow', 'crop-modal-overlay'],
    ]) {
      assert.equal(stencil[fn](), stencil, `${fn} chains`);
      assert.ok(isOpen(overlay), `${fn} opened ${overlay}`);
    }
    stencil.closeWindow();
    assert.equal(stencil.openedWindow, null);
  });
});

test('openWindow refuses a window whose control is disabled or absent, with the control\'s reason', () => {
  withWindows(({ shells }) => {
    const stencil = createStencil(makeApp());
    assert.throws(() => stencil.openKeywordsWindow(), /Save the project first/);
    assert.ok(!shells['keywords-overlay'].__stencilModal.isOpen());
    assert.throws(() => stencil.openCropWindow(), /its control is disabled/);
    assert.throws(() => stencil.openWindow('links'), /not available here/);
  }, { disabled: { 'keywords-btn': 'Save the project first to add keywords', 'crop-image': true },
       missing: ['links-btn'] });
});
