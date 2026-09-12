// The theme's two shared control treatments, pinned against the CSS — and against the
// extension's port of it, which has to look the same (lib/theme.css). Desktop twins:
// theme.cpp's QLineEdit/QComboBox:hover rule and mainWindowTheme.cpp toolButtonIconColor.
//   • a FIELD rings on hover at twice its resting border — the second pixel an inset
//     shadow, never a thicker border: a field's height is content-driven, so a fatter
//     border grew the box and pushed its row;
//   • an idle ICON button wears the accent on its glyph, but never while it is disabled
//     (a dead control must not read as live) or active (its accent FILL says "on", under
//     a white glyph).
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { LAYOUT_CSS, COMPONENTS_CSS, extensionThemeCss } from './helpers/css.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
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
  // The three that open dialogs — shortcuts, visual styles, help — the theme switch and
  // fullscreen all act on the spot, so they wear the accent FILL every other acting button
  // has. Incognito, the one real TOGGLE left beside them, stays a ghost box until it is on
  // and shows the accent on its hover border alone (user decision). Desktop twin:
  // mainWindowTheme.cpp's toolFill / toolGhostBox pair.
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
  // Copying/saving the LAYOUT is not copying/saving the image — same icons for both read
  // as the same action twice (user report). The layout is a document; copying it goes to
  // the clipboard. Desktop twin: mainWindowTheme.cpp set(actDownloadJson_/actCopyLayout_).
  const toolbar = read('../js/ui/toolbar.js');
  // The layout FILE pair: a blank page with an arrow that says which way it travels, and
  // the arrow moves on hover (iconMotion.json file-down / file-up).
  assert.match(toolbar, /id="download-json"[^>]*>\$\{icon\('file-down'\)\}/);
  assert.match(toolbar, /id="upload-json-btn"[^>]*>\$\{icon\('file-up'\)\}/);
  assert.match(toolbar, /id="copy-json-btn"[^>]*>\$\{icon\('clipboard'\)\}/);
  // Copy leads the row, then the two file moves (down, then up).
  const data = toolbar.match(/id="copy-json-btn"[\s\S]*?id="clear-storage"/)?.[0] || '';
  assert.ok(data, 'the Data row runs from copy to the destructive one');
  assert.ok(data.indexOf('id="download-json"') < data.indexOf('id="upload-json-btn"'),
    'down before up — the pair reads as one gesture in two directions');
  // …and the destructive one names what it removes.
  assert.match(toolbar, /id="clear-storage"[\s\S]{0,200}data-title="Remove current project"/);
  // The Settings row's order: the toggles first, then the theme switch, then the dialogs.
  const row = toolbar.match(/id="incognito-toggle"[\s\S]*?id="info-btn"/)?.[0] || '';
  assert.ok(row, 'incognito opens the row and help closes it');
  assert.ok(row.indexOf('id="fullscreen-toggle"') < row.indexOf('id="theme-toggle"'),
    'fullscreen sits between incognito and the theme switch');
});

test('extension: its icon buttons are left as they were — accent fill on hover', () => {
  assert.ok(!/\.icon-btn:not\(:disabled\)[\s\S]{0,200}color:\s*var\(--accent\)/.test(extTheme),
    'no themed idle glyph was ported to the extension either');
});
