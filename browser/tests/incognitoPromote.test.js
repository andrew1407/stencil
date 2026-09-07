// Leaving incognito WITHOUT losing the work (llm-contract.md §10 / the desktop's
// promoteIncognitoToLocal).
//
// The bug: an incognito session could be published to a SERVER but never kept locally, so
// "make this a normal project" and a chat `save` both dead-ended in "incognito mode — saving
// is disabled" — with the picture and its lines stranded in a session that could not be
// saved at all. Incognito's promise is that the app writes nothing BY ITSELF; an explicit
// save from the user is not the app deciding.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from './helpers/dom.js';

installDom({}, {
  location: { hash: '', pathname: '/', search: '' },
  history: { replaceState: () => {} },
});

const { DrawingApp } = await import('../js/core/drawingApp.js');

const makeMock = (over = {}) => {
  const calls = [];
  return {
    calls,
    image: {},                     // something on screen to keep
    activeProjectId: null,
    storage: {
      incognito: true,
      promoteTemporaryToProject() { calls.push(['promote']); over.onPromote?.(); },
      save() { calls.push(['save']); },
    },
    tabs: { reportActive(id) { calls.push(['reportActive', id]); } },
    // One sweep: incognito UI + title + the project-gated buttons (description/keywords/links).
    updateButtons() { calls.push(['updateButtons']); },
    ...over,
  };
};

const promote = (mock) => DrawingApp.prototype.promoteIncognitoToLocal.call(mock);

test('promoteIncognitoToLocal: leaves incognito, then saves what is on screen', () => {
  const mock = makeMock({ onPromote() { mock.activeProjectId = 'p1'; } });

  const id = promote(mock);

  assert.equal(mock.storage.incognito, false, 'the session is no longer incognito');
  assert.deepEqual(mock.calls.map(([n]) => n),
    ['promote', 'save', 'reportActive', 'updateButtons']);
  assert.equal(id, 'p1', 'the new project id comes back for the caller to name/open');
});

test('promoteIncognitoToLocal: a blank editor has nothing to keep, and stays incognito', () => {
  const mock = makeMock({ image: null });

  assert.equal(promote(mock), null);
  assert.deepEqual(mock.calls, [], 'nothing was written');
  assert.equal(mock.storage.incognito, true);
});
