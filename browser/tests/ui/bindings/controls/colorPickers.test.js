// The blank and project colour pickers (js/ui/bindings/controls/): a drag's `input` ticks only try
// the colour — the stage's fill and the swatch, or the name — with no reload, registry write or
// server push; `change` commits once; back at the opening colour, or a revert the page only notices
// on its next touch, the trial ends and leaves nothing behind.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../../helpers/dom.js';

const doc = installDom({ autoCreateById: true });
const canvases = [];
doc.createElement = (tag) => {
  const el = createStubElement(tag);
  if (tag === 'canvas') { el.getContext = () => ({ fillRect() {} }); el.toBlob = () => {}; canvases.push(el); }
  return el;
};
const { wireBlankColorButton } = await import('../../../../js/ui/bindings/controls/blankColorButton.js');
const { wireProjectColorButton } = await import('../../../../js/ui/bindings/controls/projectColorButton.js');
const { updateProjectTitle } = await import('../../../../js/ui/projects/window/projectTitle.js');
canvases.length = 0;   // accents.js measures colours on a canvas as it loads
doc.els.set('project-color-menu', null);

// Fresh controls per test, so no earlier test's listeners answer.
const controls = (btnId, inputId) => {
  const swatch = createStubElement('span');
  const btn = doc.register(btnId, createStubElement('button', { querySelector: () => swatch }));
  const input = doc.register(inputId, createStubElement('input', { showPicker() {} }));
  return { btn, input, swatch };
};
const tick = (input, value) => { input.value = value; input.dispatch('input'); };
const blankApp = (fills) => ({
  blankColor: '#ffffff', image: {}, canvas: { width: 4, height: 3 }, renderer: { previewFill: (c) => fills.push(c) },
});

test('the blank picker tries colours on the stage and commits one recolour on close', () => {
  const fills = [];
  const { btn, input, swatch } = controls('blank-color-btn', 'blank-color-input');
  wireBlankColorButton(blankApp(fills));
  btn.dispatch('click', { stopPropagation() {} });
  for (const v of ['#ff0000', '#ee0000', '#dd0000']) tick(input, v);
  assert.deepEqual(fills, [null, '#ff0000', '#ee0000', '#dd0000']);
  assert.equal(swatch.style.background, '#dd0000');
  assert.equal(canvases.length, 0, 'nothing reloads while the picker is open');
  input.dispatch('change');
  assert.equal(canvases.length, 1, 'one recolour on close');
  tick(input, '#ffffff');
  assert.deepEqual([fills.at(-1), swatch.style.background], [null, '#ffffff'], 'back at the fill: no trial');
});

test('a pick the browser reverted in silence ends its trial on the page\'s next touch', () => {
  const fills = [];
  const { btn, input } = controls('blank-color-btn', 'blank-color-input');
  wireBlankColorButton(blankApp(fills));
  btn.dispatch('click', { stopPropagation() {} });
  tick(input, '#123456');
  input.value = '#ffffff';
  doc.dispatch('pointerdown');
  assert.equal(fills.at(-1), null);
  assert.equal((doc.listeners.pointerdown || []).length, 0, 'the touch listener is one-shot');
});

test('the project picker paints the name on trial and stores and pushes one colour on close', () => {
  const meta = { name: 'p', color: '' };
  const commits = [];
  const app = {
    activeProjectId: 'p1', storage: { incognito: false, store: { getMeta: () => meta } }, imageBaseName: 'p',
    projectTransfer: { setProjectColor: (id, c) => { commits.push([id, c]); meta.color = c; updateProjectTitle(app); } },
  };
  const { btn, input } = controls('project-color-btn', 'project-color-input');
  wireProjectColorButton(app);
  const name = doc.getElementById('project-name-input');
  btn.dispatch('click', { stopPropagation() {} });
  assert.equal(input.value, '#80868f');
  for (const v of ['#ec4899', '#db2777']) tick(input, v);
  assert.deepEqual([name.style.color, commits], ['#db2777', []], 'a trial writes nothing');
  input.dispatch('change');
  assert.deepEqual(commits, [['p1', '#db2777']]);
  assert.equal(name.style.color, '#db2777');
  tick(input, '#80868f');
  assert.equal(name.style.color, '#db2777', 'a later tick at the opening colour is no trial');
});
