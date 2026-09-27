// window.stencil ships as four MAIN-world classic scripts run in order in one registration. They
// share one namespace object on window while they load; what the page is left with must be what
// the single script left: `stencil` and nothing else.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { PAGE_API_MAIN_FILES } from '../../src/background/registrars.js';
import { PAGE_API_PARTS, importApi, loadApi, setupEnv, img } from '../helpers/pageApiEnv.js';

const NS = '__stencilPageApiParts';

test('the registration runs the four parts in the order the suites load them', () => {
  assert.deepEqual(PAGE_API_MAIN_FILES, PAGE_API_PARTS.map((p) => `src/content/${p}.js`));
  for (const file of PAGE_API_MAIN_FILES)
    assert.ok(existsSync(fileURLToPath(new URL(`../../${file}`, import.meta.url))), `${file} is missing`);
});

test('once loaded, the page holds window.stencil and no trace of the shared namespace', async () => {
  const env = setupEnv({ imgs: [img('http://cdn/a.png')] });
  const before = new Set(Object.getOwnPropertyNames(env.win));
  await importApi();
  const added = Object.getOwnPropertyNames(env.win).filter((k) => !before.has(k));
  assert.deepEqual(added, ['stencil']);
  assert.equal(Object.prototype.hasOwnProperty.call(env.win, NS), false);
  assert.equal(env.win.stencil.images.length, 1, 'the parts assembled into a working facade');
});

test('on the editor page (an untagged stencil) no part runs and nothing is left behind', async () => {
  const own = { mine: true };
  const { win } = await loadApi({ stencilPreset: own });
  assert.equal(win.stencil, own);
  assert.equal(Object.prototype.hasOwnProperty.call(win, NS), false);
});

test('a second injection over a live facade changes nothing and leaves no namespace', async () => {
  const { win } = await loadApi();
  const first = win.stencil;
  await importApi();
  assert.equal(win.stencil, first);
  assert.equal(Object.prototype.hasOwnProperty.call(win, NS), false);
});

test('an entry opened from the page routes through the facade assembled last', async () => {
  const { stencil, posted } = await loadApi({ imgs: [img('http://cdn/a.png')] });
  stencil.images[0].open();
  const open = posted.map((m) => m.message).find((m) => m && m.type === 'stencil-page-open');
  assert.equal(open.url, 'http://cdn/a.png');
});
