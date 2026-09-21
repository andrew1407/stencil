// Tests for src/lib/shellTheme.js — the palette handed to the INJECTED in-page modal
// shell (lib/overlay.js). The shell lives in someone else's page, so it can't read the
// extension's CSS variables; it gets them as data. Regression: the shell used to be
// hardcoded light-with-a-prefers-color-scheme-override, so a user whose Appearance is
// Dark on a light OS got a WHITE frame around a dark crop page.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  THEME_STORAGE_KEY, THEME_MODES, SHELL_PALETTES,
  resolveShellMode, shellPalette, shellAccent, loadShellTheme,
} from '../src/lib/prefs/shellTheme.js';
import { ACCENT_HEX, ACCENT_STORAGE_KEY } from '../src/lib/highlight/highlightColor.js';

test('the mirrored storage keys match the localStorage names lib/accent.js writes', () => {
  assert.equal(THEME_STORAGE_KEY, 'stencil_theme');
  assert.equal(ACCENT_STORAGE_KEY, 'stencil_accent');
  assert.deepEqual(THEME_MODES, ['system', 'light', 'dark']);
});

test('an explicit choice wins; only "system" asks the OS', () => {
  assert.equal(resolveShellMode('dark', false), 'dark', 'Dark stays dark on a LIGHT OS');
  assert.equal(resolveShellMode('light', true), 'light', 'Light stays light on a DARK OS');
  assert.equal(resolveShellMode('system', true), 'dark');
  assert.equal(resolveShellMode('system', false), 'light');
  assert.equal(resolveShellMode(undefined, true), 'dark', 'nothing stored → follow the OS');
  assert.equal(resolveShellMode('nonsense', false), 'light');
});

test('the palettes are the two lib/theme/palette.css palettes, and they really differ', () => {
  const keys = ['bg', 'panel', 'panel2', 'line', 'text', 'muted'];
  for (const mode of ['dark', 'light']) {
    assert.deepEqual(Object.keys(SHELL_PALETTES[mode]).sort(), [...keys].sort());
    for (const k of keys) assert.match(SHELL_PALETTES[mode][k], /^#[0-9a-f]{6}$/i);
  }
  for (const k of keys) assert.notEqual(SHELL_PALETTES.dark[k], SHELL_PALETTES.light[k]);
  // The values themselves (the shell must match the page framed inside it).
  assert.equal(SHELL_PALETTES.dark.panel, '#2b2f3a');
  assert.equal(SHELL_PALETTES.light.panel, '#ffffff');
  assert.equal(shellPalette('dark'), SHELL_PALETTES.dark);
  assert.equal(shellPalette('system', true), SHELL_PALETTES.dark);
  assert.equal(shellPalette('system', false), SHELL_PALETTES.light);
});

test('the accent key resolves to the same hex the rest of the chrome uses', () => {
  assert.equal(shellAccent('sky'), ACCENT_HEX.sky);
  assert.equal(shellAccent('violet'), '#7c3aed');
  assert.equal(shellAccent('not-an-accent'), '#7c3aed', 'unknown → the default accent');
  assert.equal(shellAccent(undefined), '#7c3aed');
});

// ── loadShellTheme reads the chrome.storage.local mirrors ──

import { installChromeStub } from './helpers/chromeStub.js';

const withStorage = async (data, fn) => {
  const stub = installChromeStub({ local: data });
  try { return await fn(); } finally { stub.restore(); }
};

test('loadShellTheme carries the mode UNRESOLVED plus the accent and both palettes', async () => {
  const t = await withStorage({ stencil_theme: 'dark', stencil_accent: 'sky' }, loadShellTheme);
  // The mode stays unresolved on purpose: only the target page can answer "what does
  // this OS prefer", so the injected shell resolves 'system' itself.
  assert.equal(t.mode, 'dark');
  assert.equal(t.accent, ACCENT_HEX.sky);
  assert.equal(t.palettes, SHELL_PALETTES);
  assert.equal(t.accents.sky, ACCENT_HEX.sky, 'the map travels too, for live accent changes');
});

test('loadShellTheme falls back cleanly when nothing is mirrored or storage throws', async () => {
  const empty = await withStorage({}, loadShellTheme);
  assert.equal(empty.mode, 'system');
  assert.equal(empty.accent, '#7c3aed');

  const junk = await withStorage({ stencil_theme: 'chartreuse', stencil_accent: 42 }, loadShellTheme);
  assert.equal(junk.mode, 'system', 'an unknown mode is ignored');
  assert.equal(junk.accent, '#7c3aed');

  const stub = installChromeStub({ storageThrows: true });
  try {
    const t = await loadShellTheme();
    assert.equal(t.mode, 'system');
    assert.equal(t.accent, '#7c3aed');
  } finally { stub.restore(); }
});
