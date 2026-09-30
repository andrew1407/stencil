// The title-bar icons copy common/config/icons.json: the incognito hat-and-glasses, the desktop
// hand-off's monitor, and the desktop incognito run's monitor wearing those glasses.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const ICONS = JSON.parse(readFileSync(new URL('../../common/config/icons.json', import.meta.url), 'utf8'));
const svg = (name) => readFileSync(new URL(`../icons/${name}`, import.meta.url), 'utf8').trim();
const inner = (text) => text.slice(text.indexOf('>') + 1, text.lastIndexOf('</svg>'));

const FILES = [['incognito-light.svg', '#424242'], ['incognito-dark.svg', '#C5C5C5']];

for (const [name, ink] of FILES) {
  test(`icons/${name} draws the canonical incognito glyph, in ${ink}`, () => {
    const text = svg(name);
    assert.equal(inner(text), ICONS.incognito, `${name} drifted from common/config/icons.json`);
    assert.match(text, new RegExp(`stroke="${ink}"`), `${name} is the wrong ink for its theme`);
    assert.match(text, /viewBox="0 0 24 24"/, 'the glyph is drawn on the shared 24 grid');
  });
}

test('the manifest points the incognito command at both files', () => {
  const manifest = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
  const command = manifest.contributes.commands.find((c) => c.command === 'stencil.openInWebIncognito');
  assert.deepEqual(command.icon, { light: 'icons/incognito-light.svg', dark: 'icons/incognito-dark.svg' });
});

const INKS = [['light', '#424242'], ['dark', '#C5C5C5']];
const GLASSES = ICONS.incognito.slice(ICONS.incognito.indexOf('<g class="ic-glasses">'));

for (const [theme, ink] of INKS) {
  test(`icons/monitor-${theme}.svg draws the canonical monitor glyph`, () => {
    const text = svg(`monitor-${theme}.svg`);
    assert.equal(inner(text), ICONS.monitor, 'drifted from common/config/icons.json');
    assert.match(text, new RegExp(`stroke="${ink}"`));
  });
  test(`icons/monitor-incognito-${theme}.svg is the monitor wearing the incognito glasses`, () => {
    const body = inner(svg(`monitor-incognito-${theme}.svg`));
    assert.ok(body.startsWith(ICONS.monitor), 'the canonical monitor first');
    assert.ok(body.includes(GLASSES), 'then the canonical glasses, unchanged, on its screen');
  });
}

test('the desktop runs sit in the title bar on those icons', () => {
  const manifest = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
  const icon = (id) => manifest.contributes.commands.find((c) => c.command === id).icon;
  assert.deepEqual(icon('stencil.runInDesktop'), { light: 'icons/monitor-light.svg', dark: 'icons/monitor-dark.svg' });
  assert.deepEqual(icon('stencil.runInDesktopIncognito'),
    { light: 'icons/monitor-incognito-light.svg', dark: 'icons/monitor-incognito-dark.svg' });
  const bar = manifest.contributes.menus['editor/title'].filter((e) => e.group === 'navigation').map((e) => e.command);
  assert.ok(bar.includes('stencil.runInDesktop') && bar.includes('stencil.runInDesktopIncognito'));
});
