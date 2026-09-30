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

import EVENTS from '../../../common/config/events.json' with { type: 'json' };
import { loadOpenInConfig } from '../../js/config/openInConfig.js';
import { installNullDom } from '../helpers/nullDom.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
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
  assert.equal(Object.keys(EVENTS).length, 15);
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

// The whole app, booted over a null DOM: whatever wires the listener, a switchToSource from the
// extension resumes the project holding that source, and never in an incognito editor.
test('a booted app answers switchToSource by resuming the project that holds the source', async (t) => {
  const dom = installNullDom();
  t.after(dom.restore);
  const { DrawingApp } = await import('../../js/core/drawingApp.js');
  const app = new DrawingApp();
  const asked = [];
  const switched = [];
  app.storage.store.findByImage = (source, name) => { asked.push([source, name]); return [{ id: 'p1', name: 'Plan' }]; };
  app.projectTransfer.switchToProject = (id) => { switched.push(id); return true; };
  dom.fire(EVENTS.switchToSource, { source: 'https://example.com/plan.png', name: 'plan.png' });
  assert.deepEqual(asked, [['https://example.com/plan.png', 'plan']], 'looked up by source and bare name');
  assert.deepEqual(switched, ['p1'], 'and switched to it, in place');
  dom.fire(EVENTS.switchToSource, {});
  app.storage.incognito = true;
  dom.fire(EVENTS.switchToSource, { source: 'https://example.com/plan.png', name: 'plan.png' });
  assert.deepEqual(switched, ['p1'], 'nothing named, or an incognito editor: no switch');
  // The constructor's config load repaints the toolbar when it settles; it must land on the null DOM.
  await loadOpenInConfig();
});

test('a projects change nudges the extension bridge with registryChanged', async () => {
  const sent = [];
  const saved = { SharedWorker: globalThis.SharedWorker, window: globalThis.window };
  globalThis.SharedWorker = class { constructor() { this.port = { start() {}, postMessage() {} }; } };
  globalThis.window = { addEventListener() {}, dispatchEvent: (e) => sent.push(e) };
  try {
    const { TabsCoordinator } = await import('../../js/core/launch/tabsCoordinator.js');
    new TabsCoordinator().projectsChanged({ id: 'p1' });
  } finally {
    Object.assign(globalThis, saved);
  }
  assert.deepEqual(sent.map((e) => [e.constructor.name, e.type, e.detail]), [['Event', EVENTS.registryChanged, undefined]],
    'one detail-free Event, so the bridge re-reads the registry itself');
});
