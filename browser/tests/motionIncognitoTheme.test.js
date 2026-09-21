// The incognito frame's four growing edges and the theme-mode states (js/ui/motion.js):
// three modes, system by default, resolved the same way by the pre-paint script.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync, readdirSync } from 'node:fs';
import { themeSwap } from '../js/ui/motion.js';
import { COMPONENTS_CSS } from './helpers/css.js';

// The frame is four edges whose LENGTH animates, so dashes draw on clockwise and retract in
// reverse; an outline can only do both at once and transform would stripe the dashes.
test('the incognito frame is four edges that grow, not a stretched outline', () => {
  const css = COMPONENTS_CSS;
  assert.ok(!/body\.incognito-mode \.canvas-viewport \{[^}]*outline:/.test(css),
    'the all-at-once outline is gone');
  const edge = css.slice(css.indexOf('.ig-edge {'), css.indexOf('\n}', css.indexOf('.ig-edge {')));
  assert.match(edge, /transition: width [^;]*, *\n? *height /, 'length is what animates');
  assert.ok(!/transform:/.test(edge), 'transform would smear the dashes into stripes');
  // Clockwise on the way in…
  for (const [sel, ms] of [['t', 0], ['r', 110], ['b', 220], ['l', 330]])
    assert.match(css, new RegExp(`body\\.incognito-mode \\.ig-${sel} \\{ --ig-delay: ${ms}ms; \\}`),
      `edge ${sel} draws at ${ms}ms`);
  // …and the rest state mirrors them, so the LAST edge drawn is the FIRST to retract.
  assert.match(css, /\.ig-t \{ --ig-delay: 330ms; \}/);
  assert.match(css, /\.ig-l \{ --ig-delay: 0ms; \}/);
  // The floating "Incognito — not saved" pill over the picture is gone: that fact now
  // rides the toolbar "?" bubble, and the frame alone marks the mode on the canvas.
  assert.ok(!css.includes('.ig-badge'), 'the on-canvas incognito pill is back');
});

test('the incognito frame markup ships all four edges, inside the canvas viewport', async () => {
  const src = readFileSync(new URL('../js/ui/mainContent.js', import.meta.url), 'utf8');
  for (const e of ['ig-t', 'ig-r', 'ig-b', 'ig-l'])
    assert.ok(src.includes(e), `mainContent renders .${e}`);
  assert.ok(!src.includes('ig-badge'), 'the on-canvas incognito pill is back');
  // Always in the DOM (the exit animation needs something to retract) and inside the
  // VIEWPORT, so it traces the whole visible canvas region rather than the picture.
  assert.ok(src.indexOf('canvas-viewport" id=') < src.indexOf('incognito-frame'),
    'the frame escaped the canvas viewport');
  assert.ok(src.indexOf('incognito-frame') < src.indexOf('canvas-container" id='),
    'the frame must precede the canvas container, not sit inside it');
});

// A three-state theme mode with 'system' as the default, twin of the desktop's
// io/fileStore.hpp themeMode and the extension's lib/shellTheme.js THEME_MODES.
test('theme mode: picking a mode that resolves to the painted palette does not animate', () => {
  const src = readFileSync(new URL('../js/ui/accent/accentController.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('setThemeMode('), src.indexOf('get themeMode()'));
  assert.match(body, /resolveThemeMode\(next\) === painted/, 'the resolved palette is compared');
  // …and the setting is still stored + announced on that path, or the picker would snap back.
  const noop = body.slice(body.indexOf('=== painted'), body.indexOf('themeSwap('));
  assert.match(noop, /localStorage\.setItem\(THEME_STORAGE_KEY, next\)/, 'the mode is still stored');
  assert.match(noop, /EVENTS\.themeChanged/, 'and still announced');
  const ext = readFileSync(new URL('../../browser-extension/src/lib/prefs/shellPrefs.js', import.meta.url), 'utf8');
  assert.match(ext, /const repaints = resolveTheme\(next\) !== resolveTheme\(readTheme\(\)\)/,
    'the extension makes the same check');
});

test('theme mode: three states, system by default, resolved against the OS', async () => {
  const { THEME_MODES, resolveThemeMode } = await import('../js/ui/accent/accentController.js');
  assert.deepEqual(THEME_MODES, ['system', 'light', 'dark']);
  // An explicit mode is taken as-is, whatever the OS says.
  assert.equal(resolveThemeMode('dark', false), 'dark');
  assert.equal(resolveThemeMode('light', true), 'light');
  // 'system' — and anything unrecognised, including a missing setting — asks the OS.
  for (const mode of ['system', undefined, null, '', 'nonsense']) {
    assert.equal(resolveThemeMode(mode, true), 'dark', `${mode} follows a dark OS`);
    assert.equal(resolveThemeMode(mode, false), 'light', `${mode} follows a light OS`);
  }
});

test('theme mode: the pre-paint script and the app agree on what "system" means', () => {
  const pre = readFileSync(new URL('../js/prePaintTheme.js', import.meta.url), 'utf8');
  // Before first paint, only an explicit light/dark wins; everything else resolves against
  // the media query — otherwise a stored 'system' would paint the literal string.
  assert.match(pre, /savedTheme === 'dark' \|\| savedTheme === 'light'/,
    'the pre-paint script treats a stored mode, not a stored palette');
  assert.match(pre, /prefersDark \? 'dark' : 'light'/, 'and falls through to the OS');
  const binder = readFileSync(new URL('../js/ui/bindings/theme.js', import.meta.url), 'utf8');
  // The OS listener has to test the MODE: keyed on "nothing stored", it stopped following
  // the moment the toggle wrote a value.
  assert.match(binder, /themeMode !== 'system'/, 'the OS is followed while the mode is system');
  // ui/ is split into feature folders, so a bare module name is looked up, not assumed flat.
const UI_DIR = new URL('../js/ui/', import.meta.url);
const uiPath = (n) => {
  const walk = (d) => readdirSync(d, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(new URL(`${e.name}/`, d)) : (e.name === n ? [new URL(e.name, d)] : []));
  return n.includes('/') ? new URL(n, UI_DIR) : walk(UI_DIR)[0];
};
const vis = (f) => readFileSync(uiPath(f), 'utf8');
  assert.match(vis('visualsMarkup.js'), /id="vs-appearance"/, 'and there is a control to get back to system');
  assert.match(vis('visualsModal.js'), /setThemeMode\(appearance\.value/, 'which writes the mode');
});
