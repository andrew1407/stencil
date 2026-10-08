// A projects-list row opens the Description window for ITS project (projectMetaModal.js openFor),
// not the one in the editor: the field loads that row's stored value, Save writes that id and
// hands the row the updated meta, and the next toolbar open is back on the active project.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const doc = installDom({}, {
  window: { matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800,
    addEventListener() {}, removeEventListener() {} },
  matchMedia: () => ({ matches: true }),
});
const box = createStubElement('div', {
  getBoundingClientRect: () => ({ left: 0, top: 0, width: 200, height: 100, right: 200, bottom: 100 }) });
doc.register('description-overlay', createStubElement('div', { querySelector: (sel) => (sel === '.app-modal' ? box : null) }));
for (const id of ['btn', 'close', 'cancel', 'save']) doc.register(`description-${id}`, createStubElement('button'));
const text = doc.register('description-text', createStubElement('textarea'));

const { StencilDescriptionModal } = await import('../../../js/ui/meta/descriptionModal.js');

const metas = { active: { id: 'active', description: 'mine' }, row: { id: 'row', description: 'theirs' } };
const writes = [];
const app = {
  activeProjectId: 'active',
  storage: { store: { getMeta: (id) => metas[id] ?? null } },
  projectTransfer: { setProjectDescription: (id, v) => { writes.push([id, v]); return { ...metas[id], description: v }; } },
};
const modal = new StencilDescriptionModal();
modal.wire(app);

test('a row opens its own project, and Save writes that id and reports the update', () => {
  const saved = [];
  modal.openFor('row', { onSaved: (m) => saved.push(m.description) });
  assert.equal(text.value, 'theirs', 'the row\'s stored description, not the open project\'s');
  text.value = 'edited';
  doc.getElementById('description-save').dispatch('click');
  assert.deepEqual(writes, [['row', 'edited']]);
  assert.deepEqual(saved, ['edited']);
});

test('after a row edit closes, the toolbar opener edits the active project again', () => {
  doc.getElementById('description-overlay').__stencilModal.open();   // the toolbar button's own open
  assert.equal(text.value, 'mine');
  doc.getElementById('description-save').dispatch('click');
  assert.deepEqual(writes.at(-1), ['active', 'mine']);
});
