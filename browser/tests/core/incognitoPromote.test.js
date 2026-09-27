// Leaving incognito WITHOUT losing the work (llm-contract.md §10, the desktop's promoteIncognitoToLocal): an
// incognito session could be published to a SERVER but never kept locally, so "make this a normal project" and
// a chat `save` both dead-ended with the picture and its lines stranded. Incognito's promise is that the app
// writes nothing BY ITSELF; an explicit save from the user is not the app deciding.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../helpers/dom.js';

installDom({}, {
  location: { hash: '', pathname: '/', search: '' },
  history: { replaceState: () => {} },
});

const { promoteIncognitoToLocal } = await import('../../js/core/launch/incognitoFlow.js');
const { Emitter } = await import('../../js/core/emitter.js');
const { CHANGE } = await import('../../js/core/app/changes.js');

const makeMock = (over = {}) => {
  const calls = [];
  // The project channel: the incognito UI, the title and the project-gated buttons follow it.
  const changes = new Emitter();
  changes.on(CHANGE.project, () => calls.push(['changed:project']));
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
    changes,
    ...over,
  };
};

const promote = (mock) => promoteIncognitoToLocal(mock);

test('promoteIncognitoToLocal: leaves incognito, then saves what is on screen', () => {
  const mock = makeMock({ onPromote() { mock.activeProjectId = 'p1'; } });

  const id = promote(mock);

  assert.equal(mock.storage.incognito, false, 'the session is no longer incognito');
  assert.deepEqual(mock.calls.map(([n]) => n),
    ['promote', 'save', 'reportActive', 'changed:project']);
  assert.equal(id, 'p1', 'the new project id comes back for the caller to name/open');
});

test('promoteIncognitoToLocal: a blank editor has nothing to keep, and stays incognito', () => {
  const mock = makeMock({ image: null });

  assert.equal(promote(mock), null);
  assert.deepEqual(mock.calls, [], 'nothing was written');
  assert.equal(mock.storage.incognito, true);
});
