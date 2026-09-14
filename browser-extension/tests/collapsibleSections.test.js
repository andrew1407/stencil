// Tests for src/lib/collapsibleSections.js — the panel accordion extracted from
// popup.js: header click/keyboard toggling, control clicks passing through, the
// per-section hooks, and the hidden ≠ collapsed rule the drag spring relies on.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createCollapsibleSections } from '../src/lib/collapsibleSections.js';
import { stubDoc, stubEl } from './helpers/domStub.js';

const stubSection = (id, { collapsed = false, hidden = false } = {}) => {
  const section = stubEl('section', { id, hidden });
  section.className = `fsection${collapsed ? ' collapsed' : ''}`;
  const head = stubEl('div', { closest: () => section });
  return { section, head };
};

const build = (specs, deps = {}) => {
  const parts = specs.map(([id, opts]) => stubSection(id, opts));
  const doc = stubDoc({
    querySelectorAll: () => parts.map((p) => p.head),
    getElementById: (id) => parts.find((p) => p.section.id === id)?.section || null,
  });
  const sections = createCollapsibleSections({ doc, ...deps });
  return { sections, parts };
};

// A click event whose target is (or is not) an interactive control inside the header.
const clickOn = (control = false) => ({ target: { closest: () => (control ? {} : null) } });

test('a header click toggles the section, aria-expanded, its hook, and the user callback', () => {
  const events = [];
  const { sections, parts } = build([['sec-a', {}]], {
    beforeToggle: (s) => events.push(`before:${s.id}`),
    onUserToggle: (id) => events.push(`user:${id}`),
  });
  sections.setHook('sec-a', (collapsed) => events.push(`hook:${collapsed}`));
  const { section, head } = parts[0];

  head.fire('click', clickOn(false));
  assert.equal(section.classList.contains('collapsed'), true);
  assert.equal(head.attrs['aria-expanded'], 'false');
  assert.deepEqual(events, ['before:sec-a', 'hook:true', 'user:sec-a']);

  head.fire('click', clickOn(false));
  assert.equal(section.classList.contains('collapsed'), false);
  assert.equal(head.attrs['aria-expanded'], 'true');
});

test('a click on a control inside the header acts on the control, not the section', () => {
  const { sections, parts } = build([['sec-a', {}]]);
  parts[0].head.fire('click', clickOn(true));
  assert.equal(sections.isCollapsed('sec-a'), false);
});

test('Enter and Space toggle from the keyboard (and are consumed)', () => {
  const { sections, parts } = build([['sec-a', {}]]);
  let prevented = 0;
  parts[0].head.fire('keydown', { key: 'Enter', preventDefault: () => prevented++ });
  assert.equal(sections.isCollapsed('sec-a'), true);
  parts[0].head.fire('keydown', { key: ' ', preventDefault: () => prevented++ });
  assert.equal(sections.isCollapsed('sec-a'), false);
  parts[0].head.fire('keydown', { key: 'a', preventDefault: () => prevented++ });
  assert.equal(prevented, 2, 'only the toggle keys are consumed');
});

test('setCollapsed is idempotent and takes the same path as a click — without the user callback', () => {
  const events = [];
  const { sections, parts } = build([['sec-a', {}]], {
    onUserToggle: (id) => events.push(`user:${id}`),
  });
  sections.setHook('sec-a', (collapsed) => events.push(`hook:${collapsed}`));
  sections.setCollapsed('sec-a', true);
  sections.setCollapsed('sec-a', true);   // already there → no second toggle
  assert.equal(parts[0].section.classList.contains('collapsed'), true);
  assert.equal(parts[0].head.attrs['aria-expanded'], 'false');
  assert.deepEqual(events, ['hook:true'], 'a programmatic open is not a user toggle');
  sections.setCollapsed('unknown', true);  // unregistered id → no-op, no throw
});

test('a HIDDEN section is absent, not collapsed — the drag spring must skip it', () => {
  const { sections } = build([['sec-a', { collapsed: true, hidden: true }]]);
  assert.equal(sections.isCollapsed('sec-a'), false);
  // …and setCollapsed(expand) therefore no-ops instead of unfolding an invisible section.
  sections.setCollapsed('sec-a', false);
  assert.equal(sections.isCollapsed('sec-a'), false);
});

test('runHook fires a section\'s side-effect directly (a peek counts as an expand)', () => {
  const events = [];
  const { sections } = build([['sec-a', {}]]);
  sections.setHook('sec-a', (collapsed) => events.push(collapsed));
  sections.runHook('sec-a', false);
  sections.runHook('nope', false);   // unregistered → silent
  assert.deepEqual(events, [false]);
});
