// Tests for js/ui/ariaLabels.js — an icon-only control's accessible name comes from the
// same text its tooltip shows, and a control that already says its name keeps it.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, createStubDocument } from './helpers/dom.js';
import { controlLabel, labelControl, nameField, labelControls } from '../js/ui/ariaLabels.js';

const control = (tag, { title, tip, text = '' } = {}) => {
  const el = createStubElement(tag, { textContent: text });
  if (title !== undefined) el.dataset.title = title;
  if (tip !== undefined) el.dataset.tip = tip;
  return el;
};

test('the name is the tooltip heading, without the keycaps or the disabled reason', () => {
  assert.equal(controlLabel(control('button', { tip: 'Projects (Ctrl+Shift+P)' })), 'Projects');
  assert.equal(controlLabel(control('button', { tip: 'Crop image (Alt+C)\n— Load an image to crop' })), 'Crop image');
  // A heading with a subtitle after " — " names the control with the heading alone.
  assert.equal(controlLabel(control('button', {
    tip: 'Save Project (.stencil) — image + layout + settings in one file',
  })), 'Save Project (.stencil)');
});

test('data-tip wins over data-title, and a control with neither has no name', () => {
  assert.equal(controlLabel(control('button', { title: 'Hide panel', tip: 'Show Last Line Points' })),
               'Show Last Line Points');
  assert.equal(controlLabel(control('button', { title: 'Hide panel' })), 'Hide panel');
  assert.equal(controlLabel(createStubElement('button')), '');
});

test('an icon-only button is named; one that says its name in text is left alone', () => {
  const icon = control('button', { title: 'Projects' });
  labelControl(icon);
  assert.equal(icon.getAttribute('aria-label'), 'Projects');

  const texted = control('button', { title: 'Open an image — local file, URL, or new blank', text: 'Open Image' });
  labelControl(texted);
  assert.equal(texted.getAttribute('aria-label'), null);
});

test('a field is named even though its content reads as text', () => {
  // A <select>'s options are its content, and a number input carries its value — neither
  // is a name, so both take one.
  const select = control('select', { title: 'Image Filter', text: 'No FilterBlack & white' });
  labelControl(select);
  assert.equal(select.getAttribute('aria-label'), 'Image Filter');

  const zoom = control('input', { title: 'Zoom %', text: '' });
  zoom.value = '100';
  labelControl(zoom);
  assert.equal(zoom.getAttribute('aria-label'), 'Zoom %');
});

test('an element labelled by another one is never overwritten', () => {
  const el = control('button', { title: 'Projects' });
  el.setAttribute('aria-labelledby', 'some-heading');
  labelControl(el);
  assert.equal(el.getAttribute('aria-label'), null);
});

test('labelControls names every [data-title] under a root', () => {
  const a = control('button', { title: 'Undo' });
  const b = control('button', { title: 'Redo' });
  const doc = createStubDocument({ querySelectorAll: () => [a, b] });
  labelControls(doc);
  assert.equal(a.getAttribute('aria-label'), 'Undo');
  assert.equal(b.getAttribute('aria-label'), 'Redo');
});

// A settings row is `<div class="vs-row"><label>Name</label><span><input></span></div>`:
// the label is visible but unassociated, so the field has to point back at it.
const siblings = (...nodes) => {
  nodes.forEach((n, i) => { n.previousElementSibling = nodes[i - 1] ?? null; });
  return nodes;
};
const field = (tag = 'input', overrides = {}) =>
  createStubElement(tag, { closest: () => null, querySelector: () => null, ...overrides });

test('a field takes the name from the label before it, without making it clickable', () => {
  const label = createStubElement('label', { textContent: 'Line thickness', querySelector: () => null });
  const input = field();
  siblings(label, input);
  nameField(input);
  assert.ok(label.id, 'the label is given an id to be pointed at');
  assert.equal(input.getAttribute('aria-labelledby'), label.id);
  assert.equal(label.getAttribute('for'), null, 'no `for` — the label must not become a click target');
});

test('the label is found past the wrapper the field sits in', () => {
  const label = createStubElement('label', { textContent: 'Crop', querySelector: () => null });
  const wrap = createStubElement('span');
  siblings(label, wrap);
  const box = field('input');
  box.parentElement = wrap;
  nameField(box);
  assert.equal(box.getAttribute('aria-labelledby'), label.id);
});

test('a label that belongs to the field before it is not borrowed', () => {
  const label = createStubElement('label', { textContent: 'W', querySelector: () => null });
  const first = field();
  const second = field();
  siblings(label, first, second);
  nameField(second);
  assert.equal(second.getAttribute('aria-labelledby'), null);
});

test('a field with no row label falls back to its own placeholder', () => {
  const search = field('input', { placeholder: 'Search settings…' });
  nameField(search);
  assert.equal(search.getAttribute('aria-label'), 'Search settings…');
});

test('a field already inside a <label>, hidden, or already named, is never touched', () => {
  const wrapped = field('input', { closest: () => createStubElement('label') });
  nameField(wrapped);
  assert.equal(wrapped.getAttribute('aria-labelledby'), null);

  const swatch = field('input', { placeholder: 'Color' });
  swatch.setAttribute('aria-hidden', 'true');
  nameField(swatch);
  assert.equal(swatch.getAttribute('aria-label'), null);

  const already = field('input', { placeholder: 'URL' });
  already.setAttribute('aria-label', 'Server URL');
  nameField(already);
  assert.equal(already.getAttribute('aria-label'), 'Server URL');
});
