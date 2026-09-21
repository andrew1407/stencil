// ── events.json drift guard ─────────────────────────────────────────────────
// The stencil:* channel names are a wire contract between modules — and, for two of them,
// between the browser app and the extension's content scripts. A typo is silent: nothing
// throws, the listener just never fires. So the names live in ONE asset, and no browser
// module may carry a literal.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import EVENTS from '../js/config/events.json' with { type: 'json' };

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const jsFiles = (dir, out = []) => {
  for (const name of readdirSync(dir).sort()) {
    const full = join(dir, name);
    if (statSync(full).isDirectory()) { if (name !== 'wasm') jsFiles(full, out); }
    else if (name.endsWith('.js')) out.push(full);
  }
  return out;
};

test('every channel is stencil:<kebab of its key>, and the names are unique', () => {
  const seen = new Set();
  for (const [key, channel] of Object.entries(EVENTS)) {
    assert.match(key, /^[a-z][A-Za-z]*$/, `key "${key}" is camelCase`);
    const kebab = key.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`);
    assert.equal(channel, `stencil:${kebab}`, `${key} names its own channel`);
    assert.equal(seen.has(channel), false, `${channel} is declared twice`);
    seen.add(channel);
  }
  assert.equal(Object.keys(EVENTS).length, 14);
});

// Prose may still NAME a channel; only code may not spell one.
const codeOnly = (src) => src.split('\n').filter((l) => !l.trimStart().startsWith('//')).join('\n');

test('no module under js/ carries a stencil:* literal — they all read the asset', () => {
  const strays = [];
  for (const file of jsFiles(join(ROOT, 'js'))) {
    for (const m of codeOnly(readFileSync(file, 'utf8')).matchAll(/['"`](stencil:[a-z-]+)['"`]/g))
      strays.push(`${file.slice(ROOT.length + 1)}: ${m[1]}`);
  }
  assert.deepEqual(strays, [], 'import EVENTS from config/events.json instead');
});

// The two channels that leave the app. The extension cannot import across subprojects, so its content script
// keeps the literal and its dataParity.test.js pins it against this asset.
test('the cross-surface channels still match the extension bridge byte-for-byte', () => {
  const bridge = readFileSync(resolve(ROOT, '../browser-extension/src/content/editorBridge.js'), 'utf8');
  assert.ok(bridge.includes(`'${EVENTS.switchToSource}'`), 'the extension dispatches switchToSource');
  assert.ok(bridge.includes(`'${EVENTS.registryChanged}'`), 'the extension listens for registryChanged');
});

test('the app listens for switchToSource and dispatches registryChanged', () => {
  const app = readFileSync(resolve(ROOT, 'js/core/drawingApp.js'), 'utf8');
  assert.ok(app.includes('window.addEventListener(EVENTS.switchToSource'));
  const tabs = readFileSync(resolve(ROOT, 'js/core/launch/tabsCoordinator.js'), 'utf8');
  assert.ok(tabs.includes('new Event(EVENTS.registryChanged)'));
});
