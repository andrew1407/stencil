// The REAL capability closures sharedChatController injects (js/llm/chatSession.js), driven
// against a recording stub app. Split from chatSession.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import { sharedChatController, forgetChatController } from '../js/llm/chatSession.js';

// The REAL injected capabilities, captured off sharedChatController: `create` is the test seam, so the
// closures under test are the production ones.
const captureCapabilities = (app) => {
  let captured;
  sharedChatController(app, { create: (opts) => { captured = opts; return {}; } });
  forgetChatController(app);
  return captured;
};

test('exportImage refuses an empty editor with words, not a canvas TypeError', async () => {
  const app = { image: null, export: { renderExportCanvas: () => ({ toDataURL: () => 'data:image/png;base64,AAA' }) } };
  const caps = captureCapabilities(app);
  await assert.rejects(caps.exportImage(), /load or create one first/);
  app.image = {};
  assert.strictEqual(await caps.exportImage(), 'data:image/png;base64,AAA');
});

test('saveProject promotes to a FRESH project under a unique name and returns it', async () => {
  const calls = [];
  const app = {
    image: {}, imageBaseName: 'photo', activeProjectId: 7,
    storage: {
      store: { nameExists: (n) => n === 'cat' },
      promoteTemporaryToProject: () => calls.push('promote'),
      save: () => calls.push('save'),
    },
    renameProject: (id, name) => { calls.push(`rename ${id} ${name}`); return true; },
    updateProjectTitle: () => calls.push('title'),
  };
  const caps = captureCapabilities(app);
  // The wanted name clashes → the suffixed one lands everywhere, and comes back.
  assert.strictEqual(await caps.saveProject('cat'), 'cat 2');
  assert.strictEqual(app.imageBaseName, 'cat 2');
  assert.deepStrictEqual(calls, ['promote', 'save', 'rename 7 cat 2', 'title']);
  // No name → the editor's own base name; blank/whitespace → 'Untitled'.
  assert.strictEqual(await caps.saveProject(''), 'cat 2');
  app.imageBaseName = '   ';
  assert.strictEqual(await caps.saveProject(null), 'Untitled');
  // Nothing loaded → a thrown message, so the op reports rather than saves air.
  app.image = null;
  await assert.rejects(caps.saveProject('x'), /no image to save/);
});

test('removeProjectNamed: unknown → note, declined → note, accepted → removed', async () => {
  let allow = false;
  const removed = [];
  const confirms = [];
  const app = {
    storage: { store: { list: () => [{ id: 3, name: 'cat' }] } },
    confirm: async (msg, opts) => { confirms.push({ msg, opts }); return allow; },
    removeProject: (id) => removed.push(id),
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.removeProjectNamed('dog'), 'no saved project named "dog"');
  assert.strictEqual(confirms.length, 0, 'nothing to confirm for a miss');
  assert.strictEqual(await caps.removeProjectNamed('cat'), 'removal canceled');
  assert.deepStrictEqual(removed, [], 'declined removes nothing');
  assert.match(confirms[0].msg, /Remove project "cat"\?/);
  assert.strictEqual(confirms[0].opts.danger, true);
  allow = true;
  assert.strictEqual(await caps.removeProjectNamed('cat'), null);
  assert.deepStrictEqual(removed, [3]);
});

test('clearWorkingImage words the confirm for what actually goes, and keeps the chat', async () => {
  let allow = true;
  const confirms = [];
  const editors = [];
  const app = {
    image: null, lines: [],
    storage: { incognito: false },
    confirm: async (msg) => { confirms.push(msg); return allow; },
    newEditor: (opts) => editors.push(opts),
  };
  const caps = captureCapabilities(app);
  // Empty editor: nothing to confirm, nothing to do.
  assert.strictEqual(await caps.clearWorkingImage(), 'nothing to remove');
  assert.strictEqual(confirms.length, 0);
  // An unsaved editor names the unsaved image; an incognito one says so instead.
  app.image = {};
  assert.strictEqual(await caps.clearWorkingImage(), null);
  assert.match(confirms[0], /the unsaved image and its lines/);
  assert.deepStrictEqual(editors, [{ keepChat: true }], 'mid-turn: the chat survives the clear');
  app.storage.incognito = true;
  allow = false;
  assert.strictEqual(await caps.clearWorkingImage(), 'removal canceled');
  assert.match(confirms[1], /the image in this incognito editor/);
  assert.strictEqual(editors.length, 1, 'declined clears nothing');
});

test('openProjectNamed: already open / declined replace / failed switch are notes', async () => {
  let allow = false;
  let switchable = true;
  const confirms = [];
  const app = {
    activeProjectId: 1, image: {}, lines: [],
    storage: { temporary: true, store: { list: () => [{ id: 1, name: 'open' }, { id: 2, name: 'other' }] } },
    confirm: async (msg) => { confirms.push(msg); return allow; },
    switchToProject: () => switchable,
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.openProjectNamed('open'), '"open" is already open');
  // A dirty unsaved temporary asks the modal's confirm first; declined = a note.
  assert.strictEqual(await caps.openProjectNamed('other'), 'open canceled');
  assert.match(confirms[0], /unsaved changes in the current tab will be replaced/);
  allow = true;
  assert.strictEqual(await caps.openProjectNamed('other'), null);
  // A saved (non-temporary) editor switches without asking.
  app.storage.temporary = false;
  assert.strictEqual(await caps.openProjectNamed('other'), null);
  assert.strictEqual(confirms.length, 2, 'no confirm when nothing unsaved is at risk');
  switchable = false;
  assert.strictEqual(await caps.openProjectNamed('other'), 'could not open "other"');
});

test('renameActiveProject: no active project and duplicate names come back as notes', async () => {
  const app = {
    activeProjectId: null,
    storage: { store: { nameExists: (n) => n === 'taken' } },
    renameProject: (id, name) => name === 'ok',
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.renameActiveProject('x'), 'no active saved project to rename');
  app.activeProjectId = 4;
  assert.strictEqual(await caps.renameActiveProject('taken'), 'a project named "taken" already exists');
  assert.strictEqual(await caps.renameActiveProject('ok'), null);
  assert.strictEqual(await caps.renameActiveProject('nope'), 'could not rename to "nope"');
});

test('setBlankColor routes saved projects through the store and gates on blankness', async () => {
  const set = [];
  const app = {
    activeProjectId: 9,
    setProjectBlankColor: (id, hex) => { set.push([id, hex]); return true; },
    activeIsBlank: () => false, image: {},
    setBlankColor: (hex) => set.push(['working', hex]),
  };
  const caps = captureCapabilities(app);
  // A saved project goes through the store setter (short #rgb normalizes to #rrggbb).
  assert.strictEqual(await caps.setBlankColor('#F00'), null);
  assert.deepStrictEqual(set, [[9, '#ff0000']]);
  // The store refusing (a non-blank project) is the §10 note.
  app.setProjectBlankColor = () => false;
  assert.strictEqual(await caps.setBlankColor('#ff0000'), 'only a blank project has a recolourable background');
  // Unsaved: only a blank working image recolours.
  app.activeProjectId = null;
  assert.strictEqual(await caps.setBlankColor('#ff0000'), 'only a blank project has a recolourable background');
  app.activeIsBlank = () => true;
  assert.strictEqual(await caps.setBlankColor('#00ff00'), null);
  assert.deepStrictEqual(set.at(-1), ['working', '#00ff00']);
});

test('clearLocalProjects counts what goes into the confirm, and empty is a note', async () => {
  let allow = false;
  let cleared = 0;
  const confirms = [];
  const list = [];
  const app = {
    storage: { store: { list: () => list } },
    confirm: async (msg) => { confirms.push(msg); return allow; },
    clearAllProjects: () => cleared++,
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.clearLocalProjects(), 'no saved projects to clear');
  list.push({ id: 1 }, { id: 2 });
  assert.strictEqual(await caps.clearLocalProjects(), 'clear canceled');
  assert.match(confirms[0], /Delete every saved local project \(2\)\?/);
  allow = true;
  assert.strictEqual(await caps.clearLocalProjects(), null);
  assert.strictEqual(cleared, 1);
});

