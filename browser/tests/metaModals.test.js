// The toolbar's "Description & attributes" section (js/ui/toolbar.js) and its two windows (descriptionModal.js,
// keywordsModal.js). Pinned: the section sits between Image and Projects and holds description → keywords →
// links; both modals wear the app-modal shell (header + × Close, hint-left / Cancel + Save footer) around
// their own field — a text area, and keywordChips.js's input plus chip well; and all three buttons gate
// together on a SAVED, non-incognito project (controlState.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from './helpers/dom.js';

import { layout } from '../js/ui/layout.js';
import { HOTKEYS_WHILE_TYPING } from '../js/ui/bindings/keys/hotkeyRules.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;
const section = (label) => {
  const at = markup.indexOf(`<div class="ctrl-section-label">${label}</div>`);
  assert.notStrictEqual(at, -1, `a "${label}" section exists`);
  const next = markup.indexOf('<div class="ctrl-section-label">', at + 1);
  return markup.slice(at, next === -1 ? undefined : next);
};

test('the Description & attributes section sits between Image and Projects, in order', () => {
  const image = markup.indexOf('<div class="ctrl-section-label">Image</div>');
  const meta = markup.indexOf('<div class="ctrl-section-label">Description &amp; attributes</div>');
  const projects = markup.indexOf('<div class="ctrl-section-label">Projects</div>');
  assert.ok(image !== -1 && meta !== -1 && projects !== -1);
  assert.ok(image < meta && meta < projects, 'Image → Description & attributes → Projects');
  const s = section('Description &amp; attributes');
  const order = ['description-btn', 'keywords-btn', 'links-btn'].map((id) => s.indexOf(`id="${id}"`));
  assert.ok(order.every((i) => i !== -1), 'all three buttons live in the section');
  assert.deepEqual([...order].sort((a, b) => a - b), order, 'description, keywords, links');
  // Moved, not copied: the Image section no longer carries the links button.
  assert.ok(!section('Image').includes('id="links-btn"'));
  assert.equal(count('id="links-btn"'), 1);
});

test('the three buttons agree on the project rule in their disabled reasons + tooltips', () => {
  const s = section('Description &amp; attributes');
  assert.match(s, /id="description-btn"[^>]*data-hk-title="openDescription"[^>]*data-title="Project description"[^>]*data-disabled-reason="Save the project first to add a description"/);
  assert.match(s, /id="keywords-btn"[^>]*data-hk-title="openKeywords"[^>]*data-title="Project keywords"[^>]*data-disabled-reason="Save the project first to add keywords"/);
  assert.match(s, /id="links-btn"[^>]*data-hk-title="openLinks"[^>]*data-disabled-reason="Save the project first to add links"/);
  // Live sync is greyed out for the same reason as delete-project: nothing is linked yet.
  assert.match(markup, /id="live-sync-btn"[^>]*data-disabled-reason="Open or save a .stencil file first"/);
  assert.ok(!markup.includes('Open an image first to edit its links'), 'the old image-gated reason is gone');
  // The windows autofocus a textarea, so their chords must work from inside it.
  for (const id of ['openLinks', 'openDescription', 'openKeywords']) assert.ok(HOTKEYS_WHILE_TYPING.includes(id));
});

test('description and keywords modals share the app-modal shell', () => {
  for (const [name, ids] of [
    ['description', ['description-overlay', 'description-close', 'description-text', 'description-cancel', 'description-save']],
    ['keywords', ['keywords-overlay', 'keywords-close', 'keywords-input', 'keywords-add', 'keywords-chips', 'keywords-clear', 'keywords-cancel', 'keywords-save']],
  ]) {
    for (const id of ids) assert.equal(count(`id="${id}"`), 1, `${id} appears once`);
    const start = markup.indexOf(`id="${name}-overlay"`);
    const end = markup.indexOf('</stencil-', start);
    const m = markup.slice(start, end);
    assert.ok(m.startsWith(`id="${name}-overlay" class="app-modal-overlay"`), 'the shared overlay host');
    assert.ok(m.includes('<div class="app-modal">'));
    assert.match(m, new RegExp(`<div class="settings-header">\\s*<h2>.*?</h2>\\s*<button class="app-modal-close btn-icon-text" id="${name}-close">.*?<span>Close</span></button>`, 's'));
    // The field owns the body, so the window's every extra pixel goes to it.
    assert.ok(m.includes('<div class="settings-body meta-body">'), `${name}: the field fills the body`);
    // Footer: hint left (keywords carries none), then the field's own verb (if any) +
    // Cancel + accent Save right.
    assert.match(m, new RegExp(`<div class="settings-footer">\\s*<span class="footer-hint">[^<]*</span>\\s*<span class="chat-settings-actions">\\s*(<button id="${name}-clear".*?</button>\\s*)?<button id="${name}-cancel" class="btn-icon-text">.*?<span>Cancel</span></button>\\s*<button id="${name}-save" class="btn-icon-text primary">.*?<span>Save</span></button>`, 's'));
  }
  // Description: one text area filling the body.
  assert.match(markup, /<div class="settings-body meta-body"><textarea id="description-text" class="confirm-prompt-input meta-text" rows="\d+" placeholder="Describe this project…"><\/textarea><\/div>/);
  // Neither window carries a footer caption — the field says what it is.
  assert.match(markup, /id="description-overlay"[\s\S]*?<span class="footer-hint"><\/span>/);
  // Keywords: the word being proposed, over the well the chips are rendered into.
  assert.match(markup, /<input type="text" id="keywords-input" class="confirm-prompt-input" placeholder="Add a keyword…"/);
  // No tooltip on Add: the placeholder beside it already says what it does.
  assert.match(markup, /<button id="keywords-add" class="btn-icon-text primary">.*?<span>Add<\/span><\/button>/s);
  assert.match(markup, /<div class="kw-chips" id="keywords-chips" role="list"><\/div>/);
  // Clear all is a footer verb beside Cancel, never a button over the well.
  // No tooltip on either verb: their labels say it.
  assert.match(markup, /id="keywords-clear" class="btn-icon-text danger">[\s\S]*?<span>Clear all<\/span><\/button>\s*<button id="keywords-cancel"/);
  assert.equal(count('id="description-clear"'), 0, 'only keywords carries Clear all');
  // The keywords window carries no footer caption — the field says it all.
  assert.match(markup, /id="keywords-overlay"[\s\S]*?<span class="footer-hint"><\/span>/);
  // The old free-text field is gone from the keywords window.
  assert.equal(count('id="keywords-text"'), 0);
});

// ── controlState: the project gate ───────────────────────────────────────────
const makeApp = (over = {}) => ({
  image: null, lines: [], isDrawing: false, currentLine: null,
  activeProjectId: null, storage: { incognito: false },
  history: { canUndo: () => false, canRedo: () => false },
  remoteLink: null, compareReadOnly: () => false, openInAvailable: () => false,
  syncDrawToggleUI() {}, syncDrawModeUI() {}, updateStencilSyncUI() {},
  updateIncognitoUI() {}, updateProjectTitle() {}, renderLinesList() {},
  ...over,
});
const META_BTNS = ['description-btn', 'keywords-btn', 'links-btn'];

test('the meta buttons are enabled only for a saved, non-incognito project', async () => {
  const doc = installDom({ autoCreateById: true });
  const { DrawingApp } = await import('../js/core/drawingApp.js');
  const run = (over) => { DrawingApp.prototype.updateButtons.call(makeApp(over)); return META_BTNS.map((id) => doc.getElementById(id).disabled); };
  // An image alone is not enough — a temporary editor has no meta to attach to.
  assert.deepEqual(run({ image: { width: 10, height: 10 } }), [true, true, true]);
  assert.deepEqual(run({}), [true, true, true]);
  assert.deepEqual(run({ image: { width: 10, height: 10 }, activeProjectId: 7 }), [false, false, false]);
  // Incognito never persists a project, so nothing to describe / tag / link.
  assert.deepEqual(run({ image: { width: 10, height: 10 }, activeProjectId: 7, storage: { incognito: true } }), [true, true, true]);
  doc.restore();
});
