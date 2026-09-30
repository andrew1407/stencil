// The "Make a copy" confirmation (ui/modal/copyProjectModal.js): the local-copy box only on a
// server source, incognito refused on a server copy, Just copy refused for an incognito copy,
// and each button handing core/project/copy its request.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const IDS = ['close', 'question', 'local-row', 'local', 'local-label', 'incognito-row', 'incognito', 'incognito-label',
  'cancel', 'just', 'newtab', 'open'];

const setup = ({ meta = { id: 'p1', name: 'Cat' } } = {}) => {
  const doc = installDom({}, {
    window: { matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800,
      addEventListener() {}, removeEventListener() {}, open: () => null },
    matchMedia: () => ({ matches: true }),
  });
  const els = {};
  for (const id of IDS) els[id] = doc.register(`copy-project-${id}`, createStubElement(id === 'local' || id === 'incognito' ? 'input' : 'button'));
  const calls = [];
  const app = {
    imageBaseName: 'Cat', activeProjectId: null, remoteLink: null,
    storage: { store: { getMeta: (id) => (id === meta.id ? meta : null), list: () => [meta] } },
    projectTransfer: { copyProject: async (call) => { calls.push(call); return 'n1'; } },
  };
  return { els, app, calls };
};

const { StencilCopyProjectModal } = await import('../../../js/ui/modal/copyProjectModal.js');

const mount = (app, els) => {
  const overlay = createStubElement('stencil-copy-project-modal', {
    querySelector: (sel) => (sel === '.app-modal' ? createStubElement('div', {
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 200, height: 100, bottom: 100 }) }) : null),
  });
  const modal = Object.assign(new StencilCopyProjectModal(), overlay);
  modal.$ = (id) => els[id.replace('copy-project-', '')];
  modal.wire(app);
  return modal;
};

const change = (box, checked) => { box.checked = checked; box.dispatch('change'); };

test('a local source hides the local-copy box and names the copy it will make', () => {
  const { els, app } = setup();
  mount(app, els).openFor({ id: 'p1', what: 'layout' });
  assert.equal(els['local-row'].style.display, 'none');
  assert.equal(els.question.textContent, 'Copy “Cat” (image and layout) as “Cat-copy”?');
  assert.equal(els.incognito.disabled, false);
  assert.equal(els.just.disabled, false);
});

test('a server source shows the box; unticked it copies on the server, so incognito is off', () => {
  const { els, app } = setup({ meta: { id: 'p1', name: 'Cat', address: 'https://srv', remoteId: 'r1' } });
  mount(app, els).openFor({ id: 'p1', what: 'image' });
  assert.equal(els['local-row'].style.display, '');
  assert.match(els['local-label'].dataset.title, /instead of on https:\/\/srv/);
  assert.equal(els.incognito.disabled, true);
  change(els.local, true);
  assert.equal(els.incognito.disabled, false, 'a local copy may be incognito');
  change(els.incognito, true);
  assert.equal(els.just.disabled, true, 'an incognito copy exists only once opened');
  change(els.local, false);
  assert.deepEqual([els.incognito.disabled, els.incognito.checked, els.just.disabled], [true, false, false]);
});

test('each button hands the copy its open mode and the two boxes', async () => {
  const { els, app, calls } = setup();
  const done = [];
  const modal = mount(app, els);
  for (const [btn, how] of [['just', 'none'], ['open', 'here'], ['newtab', 'newtab']]) {
    modal.openFor({ id: 'p1', what: 'project', onDone: (id, open) => done.push([id, open]) });
    if (how === 'here') change(els.incognito, true);
    els[btn].dispatch('click');
    await new Promise((r) => setTimeout(r, 0));
  }
  assert.deepEqual(calls.map((c) => [c.id, c.what, c.open, c.incognito, c.local]), [
    ['p1', 'project', 'none', false, false],
    ['p1', 'project', 'here', true, false],
    ['p1', 'project', 'newtab', false, false],
  ]);
  assert.deepEqual(done, [['n1', 'none'], ['n1', 'here'], ['n1', 'newtab']]);
});

test('a failed copy closes the tab opened for it and leaves the list alone', async () => {
  const { els, app } = setup();
  const win = { closed: false, close() { this.closed = true; } };
  globalThis.window.open = () => win;
  app.projectTransfer.copyProject = async () => { throw new Error('offline'); };
  const done = [];
  const modal = mount(app, els);
  modal.openFor({ id: 'p1', what: 'layout', onDone: (...a) => done.push(a) });
  els.newtab.dispatch('click');
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(win.closed, true);
  assert.deepEqual(done, []);
});
