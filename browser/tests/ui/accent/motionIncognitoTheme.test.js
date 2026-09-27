// The incognito frame's four growing edges and the theme-mode states (js/ui/motion.js):
// three modes, system by default, resolved the same way by the pre-paint script.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { THEME_SWAP_CLASS } from '../../../js/ui/motion.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { createStubElement, installDom } from '../../helpers/dom.js';
import { installMemoryStorage } from '../../helpers/memoryStorage.js';

const store = installMemoryStorage();
// A stub page with an OS colour scheme: `os.dark` answers the media query, `os.flip` fires its change.
const withPage = async (run) => {
  const os = { dark: false, listeners: [] };
  const mql = { get matches() { return os.dark; }, addEventListener: (t, fn) => os.listeners.push(fn) };
  os.flip = (dark) => { os.dark = dark; os.listeners.forEach((fn) => fn({ matches: dark })); };
  const events = [];
  const doc = installDom({}, {
    window: { matchMedia: () => mql, dispatchEvent: (e) => events.push(e), addEventListener() {}, innerWidth: 800, innerHeight: 600 },
    matchMedia: () => mql,
    CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
  });
  const stub = (id) => createStubElement('div', { id, dataset: { csEnhanced: '1' }, querySelector: () => stub() });
  doc.getElementById = (id) => doc.els.get(id) ?? doc.els.set(id, stub(id)).get(id);
  store.clear();
  try { return await run({ doc, os, events }); } finally { doc.restore(); }
};

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
  const { StencilMainContent } = await import('../../../js/ui/panel/mainContent.js');
  const html = StencilMainContent.inner().replace(/<!--[\s\S]*?-->/g, '');
  const frame = /<div class="incognito-frame" aria-hidden="true">([\s\S]*?)<\/div>/.exec(html);
  assert.ok(frame, 'the frame is rendered');
  assert.deepEqual([...frame[1].matchAll(/<i class="ig-edge (ig-[a-z])"><\/i>/g)].map((m) => m[1]),
    ['ig-t', 'ig-r', 'ig-b', 'ig-l'], 'all four edges, clockwise from the top');
  assert.ok(!html.includes('ig-badge'), 'the on-canvas incognito pill is back');
  // Always in the DOM (the exit animation needs something to retract) and inside the
  // VIEWPORT, so it traces the whole visible canvas region rather than the picture.
  const viewportAt = html.indexOf('<div class="canvas-viewport" id="canvas-viewport">');
  assert.ok(viewportAt !== -1 && viewportAt < frame.index, 'the frame escaped the canvas viewport');
  assert.ok(frame.index < html.indexOf('<div class="canvas-container" id="canvas-container">'),
    'the frame must precede the canvas container, not sit inside it');
});

// A three-state theme mode with 'system' as the default, twin of the desktop's
// io/fileStore.hpp themeMode and the extension's lib/prefs/shellTheme.js THEME_MODES.
test('theme mode: picking a mode that resolves to the painted palette does not animate', async () => {
  const { AccentController, THEME_STORAGE_KEY } = await import('../../../js/ui/accent/controller.js');
  const { EVENTS } = await import('../../../js/eventBus/appBus.js');
  const run = (painted, dark, mode) => withPage(({ doc, os, events }) => {
    const root = doc.documentElement;
    root.setAttribute('data-theme', painted);
    os.dark = dark;
    new AccentController({ get theme() { return root.getAttribute('data-theme'); } }).setThemeMode(mode);
    return { swapped: root.classList.contains(THEME_SWAP_CLASS), theme: root.getAttribute('data-theme'),
      stored: localStorage.getItem(THEME_STORAGE_KEY), announced: events.map((e) => [e.type, e.detail]) };
  });
  const same = await run('light', false, 'system');
  assert.equal(same.swapped, false, 'the resolved palette is compared: no wipe');
  assert.equal(same.theme, 'light');
  // …and the setting is still stored + announced on that path, or the picker would snap back.
  assert.equal(same.stored, 'system', 'the mode is still stored');
  assert.deepEqual(same.announced, [[EVENTS.themeChanged, 'system']], 'and still announced');
  const other = await run('light', true, 'system');
  assert.equal(other.swapped, true, 'a mode that repaints does animate');
  assert.equal(other.theme, 'dark');
  const ext = readFileSync(new URL('../../../../browser-extension/src/lib/prefs/shellPrefs.js', import.meta.url), 'utf8');
  assert.match(ext, /const repaints = resolveTheme\(next\) !== resolveTheme\(readTheme\(\)\)/,
    'the extension makes the same check');
});

test('theme mode: three states, system by default, resolved against the OS', async () => {
  const { THEME_MODES, resolveThemeMode } = await import('../../../js/ui/accent/controller.js');
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

// prePaintTheme.js is a classic <head> script, so it runs in a context of its own.
const prePaint = (saved, dark) => {
  const attrs = new Map();
  vm.runInNewContext(readFileSync(new URL('../../../js/prePaintTheme.js', import.meta.url), 'utf8'), {
    document: { documentElement: { setAttribute: (k, v) => attrs.set(k, v) } },
    localStorage: { getItem: (k) => (k === 'drawingApp_theme' ? saved : null) },
    window: { matchMedia: () => ({ matches: dark }) },
  });
  return attrs.get('data-theme');
};

test('theme mode: the pre-paint script and the app agree on what "system" means', async () => {
  // Before first paint, only an explicit light/dark wins; everything else resolves against
  // the media query — otherwise a stored 'system' would paint the literal string.
  for (const saved of ['system', null, 'nonsense'])
    for (const dark of [true, false])
      assert.equal(prePaint(saved, dark), dark ? 'dark' : 'light', `${saved} follows the OS`);
  assert.equal(prePaint('dark', false), 'dark', 'a stored mode wins over the OS');
  assert.equal(prePaint('light', true), 'light');
  const { resolveThemeMode } = await import('../../../js/ui/accent/controller.js');
  for (const saved of ['system', 'light', 'dark'])
    assert.equal(prePaint(saved, true), resolveThemeMode(saved, true), `${saved}: the app resolves it the same way`);

  const { wireTheme } = await import('../../../js/ui/bindings/theme.js');
  const { visualsModalInner } = await import('../../../js/ui/visuals/markup.js');
  const { StencilVisualsModal } = await import('../../../js/ui/visuals/modal.js');
  const followed = await withPage(({ doc, os }) => {
    const root = doc.documentElement;
    const accents = { themeMode: 'system', updateThemeIcon() {}, setThemeMode: (m) => picks.push(m) };
    const picks = [];
    wireTheme({ accents, accent: 'violet' });
    root.setAttribute('data-theme', 'light');
    os.flip(true);
    const whileSystem = root.getAttribute('data-theme');
    // The OS listener tests the MODE, not whether anything is stored: an explicit pick stops it following.
    accents.themeMode = 'light';
    root.setAttribute('data-theme', 'light');
    os.flip(true);
    const whileExplicit = root.getAttribute('data-theme');
    StencilVisualsModal.prototype.wire.call({}, {
      accents, settings: { setMotion() {}, setNotifyChannel() {}, setVisualColor() {} },
      setAccent() {}, renderer: { redraw() {} }, storage: { saveSoon() {} }, input: { setHoldDrawDelay() {} },
    });
    const appearance = doc.getElementById('vs-appearance');
    appearance.value = 'system';
    appearance.dispatch('change');
    return { whileSystem, whileExplicit, picks };
  });
  assert.equal(followed.whileSystem, 'dark', 'the OS is followed while the mode is system');
  assert.equal(followed.whileExplicit, 'light', 'and not once a mode is chosen');
  assert.match(visualsModalInner(), /<select id="vs-appearance">\s*<option value="system">/,
    'and there is a control to get back to system');
  assert.deepEqual(followed.picks, ['system'], 'which writes the mode');
});
