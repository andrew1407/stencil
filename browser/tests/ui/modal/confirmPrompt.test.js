// app.prompt's live validation (ui/confirmModal.js) — the desktop's PromptSpec::validate. A reason disables
// Confirm and Enter and says why under the field, so a prompt can never offer an action its caller will then
// ignore for an empty answer (user report).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const IDS = ['confirm-modal-close', 'confirm-modal-cancel', 'confirm-modal-confirm',
             'confirm-modal-title-text', 'confirm-modal-title-icon', 'confirm-modal-message',
             'confirm-modal-confirm-text', 'confirm-modal-cancel-text'];

const setup = () => {
  const body = createStubElement('div');
  const box = createStubElement('div', {
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 200, height: 100, bottom: 100 }),
  });
  const overlay = createStubElement('div', {
    querySelector: (sel) => (sel === '.settings-body' ? body : sel === '.app-modal' ? box : null),
  });
  const doc = installDom({
    // The component focuses and selects its field on a 30ms timer; the bare stub has
    // neither method and the deferred call would fail the test after it had passed.
    createElement: (tag) => createStubElement(tag, { focus() {}, select() {} }),
  }, {
    window: { matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800,
              addEventListener() {}, removeEventListener() {} },
    matchMedia: () => ({ matches: true }),
  });
  doc.register('confirm-modal-overlay', overlay);
  for (const id of IDS) doc.register(id, createStubElement('button', { id }));
  return { doc, body, overlay, confirm: doc.getElementById('confirm-modal-confirm') };
};

const { StencilConfirmModal } = await import('../../../js/ui/modal/confirmModal.js');

// The injected row is a wrapper div holding the field and (when validating) the reason.
const fieldOf = (body) => body.children.at(-1).children.find((c) => c.tagName !== 'DIV');
const reasonOf = (body) => body.children.at(-1).children.find((c) => c.className === 'confirm-prompt-reason');

test('a validating prompt starts refused and clears as soon as the value is good', () => {
  const { body, confirm } = setup();
  const modal = new StencilConfirmModal();
  modal.wire();
  modal.prompt('Paste a token', { validate: (v) => (v ? '' : 'Paste a token to reconnect') });

  const input = fieldOf(body);
  assert.ok(input, 'the prompt injected its field');
  assert.equal(confirm.disabled, true, 'an empty box offers no Confirm');
  assert.equal(reasonOf(body).textContent, 'Paste a token to reconnect', 'and says why');
  assert.equal(confirm.dataset.title, 'Paste a token to reconnect', 'the reason is the button tooltip too');

  input.value = 'tok';
  input.dispatch('input');
  assert.equal(confirm.disabled, false, 'a value re-arms it');
  assert.equal(reasonOf(body).style.display, 'none', 'and the reason goes');

  input.value = '   ';   // whitespace is not a token: validate sees the TRIMMED text
  input.dispatch('input');
  assert.equal(confirm.disabled, true);
});

test('a prompt with no validate behaves exactly as before', () => {
  const { body, confirm } = setup();
  const modal = new StencilConfirmModal();
  modal.wire();
  modal.prompt('Name it');
  assert.equal(confirm.disabled, false, 'nothing to refuse, nothing disabled');
  assert.equal(reasonOf(body), undefined, 'and no reason line is built at all');
});
