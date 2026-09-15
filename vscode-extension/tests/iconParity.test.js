// The incognito title-bar icon copies browser/js/config/icons.json's hat-and-glasses glyph.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const ICONS = JSON.parse(readFileSync(new URL('../../browser/js/config/icons.json', import.meta.url), 'utf8'));
const svg = (name) => readFileSync(new URL(`../icons/${name}`, import.meta.url), 'utf8').trim();
const inner = (text) => text.slice(text.indexOf('>') + 1, text.lastIndexOf('</svg>'));

const FILES = [['incognito-light.svg', '#424242'], ['incognito-dark.svg', '#C5C5C5']];

for (const [name, ink] of FILES) {
  test(`icons/${name} draws the canonical incognito glyph, in ${ink}`, () => {
    const text = svg(name);
    assert.equal(inner(text), ICONS.incognito, `${name} drifted from browser/js/config/icons.json`);
    assert.match(text, new RegExp(`stroke="${ink}"`), `${name} is the wrong ink for its theme`);
    assert.match(text, /viewBox="0 0 24 24"/, 'the glyph is drawn on the shared 24 grid');
  });
}

test('the manifest points the incognito command at both files', () => {
  const manifest = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
  const command = manifest.contributes.commands.find((c) => c.command === 'stencil.openInWebIncognito');
  assert.deepEqual(command.icon, { light: 'icons/incognito-light.svg', dark: 'icons/incognito-dark.svg' });
});
