// The Appearance half of src/lib/accent.js: light/dark/system, the chrome.storage.local mirror
// other contexts read, private-mode degradation, and the cross-page storage-event sync.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { loadAccent } from '../../helpers/accentSandbox.js';
import { THEME_MODES } from '../../../src/lib/prefs/shellTheme.js';

// ── Appearance (light / dark / system) ──

test('the three modes are the ones the shell mirrors', () => {
  const { theme } = loadAccent();
  // Spread across the realm boundary: the sandbox's Array has a different prototype,
  // which strict deepEqual counts as a difference.
  assert.deepEqual([...theme.modes], ['system', 'light', 'dark']);
  assert.deepEqual([...theme.modes], THEME_MODES);
});

test('defaults to system, and resolves against the OS', () => {
  const light = loadAccent({ prefersDark: false });
  assert.equal(light.theme.get(), 'system');
  assert.equal(light.theme.resolved(), 'light');
  assert.equal(light.dataTheme(), 'light');

  const dark = loadAccent({ prefersDark: true });
  assert.equal(dark.theme.get(), 'system');
  assert.equal(dark.theme.resolved(), 'dark');
  assert.equal(dark.dataTheme(), 'dark');
});

test('an explicit choice beats the OS in both directions', () => {
  const onDarkOs = loadAccent({ stored: { stencil_theme: 'light' }, prefersDark: true });
  assert.equal(onDarkOs.theme.get(), 'light');
  assert.equal(onDarkOs.dataTheme(), 'light', 'Light must stay light on a dark OS');

  const onLightOs = loadAccent({ stored: { stencil_theme: 'dark' }, prefersDark: false });
  assert.equal(onLightOs.dataTheme(), 'dark', 'Dark must stay dark on a light OS');
});

test('the CHOSEN mode is stored but the RESOLVED one is stamped', () => {
  const page = loadAccent({ prefersDark: true });
  page.theme.set('system');
  assert.equal(page.store.get('stencil_theme'), 'system', 'storage keeps the choice');
  assert.equal(page.dataTheme(), 'dark', 'the attribute carries the resolution');
});

test('set() sanitises an unknown mode to system', () => {
  const page = loadAccent({ prefersDark: true });
  assert.equal(page.theme.set('sepia'), 'system');
  assert.equal(page.store.get('stencil_theme'), 'system');
  assert.equal(page.dataTheme(), 'dark');
});

test('system keeps tracking the OS after load', () => {
  const page = loadAccent({ prefersDark: false });
  assert.equal(page.dataTheme(), 'light');
  page.setPrefersDark(true);
  assert.equal(page.dataTheme(), 'dark', 'the OS flipping must re-stamp the attribute');
});

test('an explicit choice ignores the OS flipping', () => {
  const page = loadAccent({ stored: { stencil_theme: 'light' }, prefersDark: false });
  page.setPrefersDark(true);
  assert.equal(page.dataTheme(), 'light', 'Light must survive the OS going dark');
});

// ── Mirroring into chrome.storage.local ──

test('both choices are mirrored for contexts that cannot read this localStorage', () => {
  const page = loadAccent({ stored: { stencil_accent: 'brown', stencil_theme: 'dark' } });
  const merged = Object.assign({}, ...page.mirrored);
  assert.equal(merged.stencil_accent, 'brown');
  assert.equal(merged.stencil_theme, 'dark');
});

test('a sanitised accent is mirrored as the sanitised value, not the raw one', () => {
  const page = loadAccent({ stored: { stencil_accent: 'bogus' } });
  const merged = Object.assign({}, ...page.mirrored);
  assert.equal(merged.stencil_accent, 'violet');
});

test('no chrome API (an ordinary page) is not fatal', () => {
  const page = loadAccent({ withChrome: false });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.mirrored.length, 0);
});

// ── Private mode ──

test('a throwing localStorage degrades to the defaults instead of breaking the page', () => {
  const page = loadAccent({ storageThrows: true, prefersDark: true });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.theme.get(), 'system');
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.dataTheme(), 'dark', 'the OS preference still resolves');
});

test('writes are swallowed in private mode, and the attribute still updates', () => {
  const page = loadAccent({ storageThrows: true });
  assert.equal(page.accent.set('pink'), 'pink');
  assert.equal(page.dataAccent(), 'pink', 'the visual change applies for this page session');
  assert.equal(page.theme.set('dark'), 'dark');
  assert.equal(page.dataTheme(), 'dark');
});

test('no matchMedia is not fatal — it resolves light', () => {
  const page = loadAccent({ withMatchMedia: false });
  assert.equal(page.theme.resolved(), 'light');
  assert.equal(page.dataTheme(), 'light');
});

// ── Cross-page live sync ──

test('another page changing the accent re-applies here without a reload', () => {
  const page = loadAccent();
  assert.equal(page.dataAccent(), 'violet');
  // Another same-origin extension page wrote the key; the storage event fires here.
  page.store.set('stencil_accent', 'orange');
  page.fireStorage('stencil_accent');
  assert.equal(page.dataAccent(), 'orange');
});

test('another page changing the Appearance re-applies here', () => {
  const page = loadAccent({ prefersDark: false });
  page.store.set('stencil_theme', 'dark');
  page.fireStorage('stencil_theme');
  assert.equal(page.dataTheme(), 'dark');
});

test('localStorage.clear() elsewhere (key === null) resets both to their defaults', () => {
  const page = loadAccent({ stored: { stencil_accent: 'grey', stencil_theme: 'dark' } });
  assert.equal(page.dataAccent(), 'grey');
  page.store.clear();
  page.fireStorage(null);
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.dataTheme(), 'light');
});

test('an unrelated key is ignored', () => {
  const page = loadAccent({ stored: { stencil_accent: 'aqua' } });
  page.store.set('stencil_accent', 'pink'); // changed underneath, but…
  page.fireStorage('someone_elses_key');    // …the event is for another key
  assert.equal(page.dataAccent(), 'aqua', 'only the accent/theme keys should trigger a re-apply');
});

test('onChange fires when another surface changes the Appearance', () => {
  const page = loadAccent();
  const seen = [];
  page.theme.onChange((mode) => seen.push(mode));
  page.store.set('stencil_theme', 'dark');
  page.fireStorage('stencil_theme');
  assert.deepEqual(seen, ['dark']);
});
