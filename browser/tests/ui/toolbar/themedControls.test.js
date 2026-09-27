// The theme's two shared control treatments, pinned against the CSS and against the extension's port of it
// (lib/theme/); desktop twins: theme.cpp's QLineEdit/QComboBox:hover rule and MainWindowTheme.cpp
// toolButtonIconColor. A FIELD rings on hover at twice its resting border, the second pixel an inset shadow
// and never a thicker border, which would grow a content-driven box; an idle ICON button wears the accent on
// its glyph, but never while it is disabled (a dead control must not read as live) or active.
import test from 'node:test';
import assert from 'node:assert';
import { LAYOUT_CSS, COMPONENTS_CSS, extensionThemeCss } from '../../helpers/css.js';
import { layout as appLayout } from '../../../js/ui/layout.js';
import { icon } from '../../../js/ui/icons.js';

// A toolbar button as rendered: its open tag, and what it holds.
const MARKUP = appLayout();
const button = (id) => {
  const at = MARKUP.indexOf(`<button id="${id}"`);
  assert.ok(at >= 0, `no #${id} button is rendered`);
  const open = MARKUP.indexOf('>', at) + 1;
  return { tag: MARKUP.slice(at, open), inner: MARKUP.slice(open, MARKUP.indexOf('</button>', open)) };
};
const layout = LAYOUT_CSS;
const components = COMPONENTS_CSS;
const extTheme = extensionThemeCss();

const ringRuleOf = (css) =>
  css.match(/input\[type="text"\]:hover:not\(:disabled\):not\(:focus\),[\s\S]*?\}/)?.[0] || '';

for (const [name, css] of [['browser', layout], ['extension', extTheme]]) {
  test(`${name}: a field rings on hover without moving`, () => {
    const rule = ringRuleOf(css);
    assert.ok(rule, 'the shared hover-ring rule is there');
    for (const sel of ['input[type="number"]', 'input[type="search"]', 'textarea', 'select'])
      assert.ok(rule.includes(sel), `${sel} rings too — every text/number field does`);
    assert.match(rule, /border-color:\s*color-mix\(in srgb, var\(--accent\) 45%, transparent\)/,
      'in the accent at the ring strength the desktop uses (rgba(accent, .45))');
    assert.match(rule, /box-shadow:\s*inset 0 0 0 1px/,
      'the second pixel is an inset shadow — a thicker border would grow the box');
    assert.ok(!/border-width/.test(rule), 'so the border itself is never fattened');
    assert.ok(rule.includes(':not(:disabled)') && rule.includes(':not(:focus)'),
      'never on a disabled field, never over the focus ring');
  });
}

test('browser: what a Settings-row button looks like says what it DOES', () => {
  // The buttons that act on the spot wear the accent FILL every acting button has; incognito, the one real
  // TOGGLE beside them, stays a ghost box until it is on. Desktop twin: toolFill / toolGhostBox.
  const filled = components.match(/#settings-btn, #visuals-btn, #info-btn \{[\s\S]*?\}/)?.[0] || '';
  assert.match(filled, /background:\s*var\(--accent\)/, 'the dialog-openers are filled');
  assert.match(filled, /color:\s*var\(--on-accent\)/, '…with the glyph in the accent\'s own ink');
  const theme = layout.match(/#theme-toggle \{[\s\S]*?\}/)?.[0] || '';
  assert.match(theme, /background:\s*var\(--accent\)/, 'and so is the theme switch');
  assert.ok(!/#settings-btn:not\(:disabled\):not\(\.active\)[\s\S]{0,400}color:\s*var\(--accent\)/.test(components),
    'no accent on an idle GLYPH anywhere — that read as everything being on');

  // Fullscreen is filled either way: its glyph — maximize in, minimize out — is what says
  // which way the click goes.
  const fs = components.match(/#fullscreen-toggle \{[\s\S]*?\}/)?.[0] || '';
  assert.match(fs, /background:\s*var\(--accent\)/, 'fullscreen is filled at rest too');
  assert.match(fs, /color:\s*var\(--on-accent\)/, '…with the glyph in the accent\'s own ink');
  // Incognito is the one real TOGGLE left in the row: a ghost until it is on.
  const ghostHover = components.match(/#incognito-toggle:not\(\.active\):not\(:disabled\):hover[\s\S]*?\}/)?.[0] || '';
  assert.match(ghostHover, /border-color:\s*var\(--accent\)\s*!important/,
    'its resting border is itself !important, so the hover must be');
  const inset = components.match(/#chat-btn:not\(\.active\):not\(:disabled\):hover,[\s\S]*?\}/)?.[0] || '';
  assert.match(inset, /box-shadow:\s*inset 0 0 0 1px var\(--accent\)/,
    'the two that ring with an inset shadow take the accent there');
  assert.ok(inset.includes('#voice-chat-btn'));
  // Fit-to-window ACTS, so it is filled like the rest of them (its ring is an inset
  // shadow rather than a border: a border would move the row on toggle).
  const fit = components.match(/#zoom-fit \{[\s\S]*?\}/)?.[0] || '';
  assert.match(fit, /background:\s*var\(--accent\)/);
  assert.match(fit, /box-shadow:\s*inset 0 0 0 1px var\(--accent\)/);
});

test('browser: the DATA section does not borrow the IMAGE section\'s glyphs', () => {
  // Copying or saving the LAYOUT is not copying or saving the image, so the icons differ (user report): the
  // layout is a document. Desktop twin: MainWindowTheme.cpp set(actDownloadJson_/actCopyLayout_).
  // The layout FILE pair: a blank page with an arrow that says which way it travels, and
  // the arrow moves on hover (iconMotion.json file-down / file-up).
  assert.equal(button('download-json').inner, icon('file-down'));
  assert.equal(button('upload-json-btn').inner, icon('file-up'));
  assert.equal(button('copy-json-btn').inner, icon('clipboard'));
  // Copy leads the row, then the two file moves (down, then up).
  const at = (id) => MARKUP.indexOf(`<button id="${id}"`);
  assert.ok(at('copy-json-btn') < at('clear-storage'), 'the Data row runs from copy to the destructive one');
  assert.ok(at('copy-json-btn') < at('download-json') && at('upload-json-btn') < at('clear-storage'));
  assert.ok(at('download-json') < at('upload-json-btn'),
    'down before up — the pair reads as one gesture in two directions');
  // …and the destructive one names what it removes.
  assert.match(button('clear-storage').tag, /data-title="Remove current project"/);
  // The Settings row's order: the toggles first, then the theme switch, then the dialogs.
  assert.ok(at('incognito-toggle') < at('info-btn'), 'incognito opens the row and help closes it');
  assert.ok(at('incognito-toggle') < at('fullscreen-toggle') && at('info-btn') > at('theme-toggle'));
  assert.ok(at('fullscreen-toggle') < at('theme-toggle'),
    'fullscreen sits between incognito and the theme switch');
});

test('extension: its icon buttons are left as they were — accent fill on hover', () => {
  assert.ok(!/\.icon-btn:not\(:disabled\)[\s\S]{0,200}color:\s*var\(--accent\)/.test(extTheme),
    'no themed idle glyph was ported to the extension either');
});
