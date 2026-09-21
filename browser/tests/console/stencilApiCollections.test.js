// Project collections and the sanitizing writers (js/console/stencilApi.js): opened /
// archived sets, the live active project, and the apply/layout prototype-pollution guards.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  createStencil, validateLayout, makeApp, called, lastCall, withProjects,
} from '../helpers/stencilApiRig.js';

// ── Project collections ───────────────────────────────────────────────────────────
test('project collections: opened / archived / getProjects honour the open set', () => {
  const app = withProjects();   // active id 1; metas Alpha(1), Beta(2)
  const stencil = createStencil(app);

  assert.deepEqual(stencil.openedProjects.map((p) => p.id), [1]);    // only the active id is open
  assert.deepEqual(stencil.archivedProjects.map((p) => p.id), [2]);  // saved but not open
  assert.deepEqual(stencil.incognitoProjects, []);                   // no incognito editor here
  assert.deepEqual(stencil.getProjects().map((p) => p.id), [1]);     // default = currently open
  assert.deepEqual(stencil.getProjects({ archived: true }).map((p) => p.id), [1, 2]);
});

test('active project: source/resource/imageName set live; size/isOpened/layout read through', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  app.image = { width: 640, height: 480 };

  const p = stencil.current;   // id 1, the active project
  assert.equal(p.isOpened, true);
  assert.deepEqual(p.size, { image: { width: 640, height: 480 } });
  assert.equal(p.imageName, 'pic.png');

  p.source = 'http://x/i.png';
  assert.equal(app.imageSource, 'http://x/i.png');
  p.resource = 'http://x/page';
  assert.equal(app.imageResource, 'http://x/page');
  p.imageName = 'renamed.png';
  assert.equal(app.imageBaseName, 'renamed.png');
  assert.ok(called(app, 'save').length >= 3);   // each live edit flushes storage

  assert.deepEqual(p.layout, {});   // store().get(1).payload.layout
});

// Prototype-pollution guards: `apply()` iterates stencil.settings' OWN keys, never Object.keys(opts),
// and the layout setter routes through validateLayout, whose sanitizeLines rebuilds from a whitelist.
test('apply() ignores a polluting __proto__ payload and never touches Object.prototype', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  // Object-literal form: `__proto__` is the object's prototype, not an own key — apply()
  // still only reads the settings namespace's keys, so nothing reaches Object.prototype.
  stencil.apply({ __proto__: { polluted: 1 }, thickness: 3 });
  assert.equal(({}).polluted, undefined);
  assert.equal(Object.prototype.polluted, undefined);
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 3]);   // legit key still routed

  // JSON-parsed form: here `__proto__` IS an own enumerable key. apply() never iterates it
  // (it walks its own key list), so it can't be re-assigned onto anything shared.
  const evil = JSON.parse('{"__proto__":{"polluted":2},"thickness":5}');
  stencil.apply(evil);
  assert.equal(({}).polluted, undefined);
  assert.equal(Object.prototype.polluted, undefined);
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 5]);
});

// ApplyOptions extends Partial<StencilSettings> (stencilApi.d.ts), so a subset would make the
// type lie: a key it skipped would be dropped in silence rather than rejected.
test('apply() routes EVERY writable settings key, and never a read-only one', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  stencil.apply({ thickness: 4, darkTheme: false, mainTheme: 'aqua', holdDrawDelay: 900,
                  pageSize: 'custom', pageWidth: 12, mainThemes: ['nope'] });
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 4]);
  assert.deepEqual(lastCall(app, 'setTheme'), ['setTheme', 'light']);
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'aqua']);
  assert.deepEqual(lastCall(app, 'setHoldDrawDelay'), ['setHoldDrawDelay', 900]);
  // 'custom' is chosen before the dimensions it applies to.
  assert.deepEqual(lastCall(app, 'setPageSize'), ['setPageSize', 'custom']);
  assert.deepEqual(lastCall(app, 'setCustomPageWidth'), ['setCustomPageWidth', 12]);
  assert.deepEqual(stencil.mainThemes.includes('nope'), false, 'a getter-only member is skipped');
});

test('layout setter routes to applyPastedLayout → validateLayout without polluting Object.prototype', () => {
  // Wire the mock's applyPastedLayout to the REAL validateLayout so the sanitize path
  // (which strips __proto__ and rebuilds lines onto fresh objects) is exercised end-to-end.
  const app = makeApp({
    image: { width: 10, height: 10 },
    canvas: { width: 10, height: 10 },
    lines: [],
    applyPastedLayout(data) {
      app.calls.push(['applyPastedLayout', data]);
      const v = validateLayout(data, { hasImage: true, imgW: 10, imgH: 10, hasExistingLines: false });
      app.lines = v.lines;
    },
  });
  const stencil = createStencil(app);

  // A JSON-parsed layout carrying an OWN "__proto__" key both at the top level and inside a
  // line entry — the dangerous shape that a naive deep-merge would splat onto Object.prototype.
  const evilLayout = JSON.parse(
    '{"__proto__":{"x":1},"lines":[{"points":[{"x":1,"y":2}],"color":"#ff0000","__proto__":{"x":2}}]}',
  );
  stencil.layout = evilLayout;

  assert.deepEqual(lastCall(app, 'applyPastedLayout')[1], evilLayout);   // routed through the facade
  assert.equal(({}).x, undefined);
  assert.equal(Object.prototype.x, undefined);

  // The sanitized line kept its real fields but carries no injected own key.
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points, [{ x: 1, y: 2 }]);
  assert.equal(app.lines[0].color, '#ff0000');
  assert.ok(!Object.prototype.hasOwnProperty.call(app.lines[0], '__proto__'));
});
