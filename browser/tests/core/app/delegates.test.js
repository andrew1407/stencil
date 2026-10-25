// DrawingApp takes on only its view seam (core/app/delegates.js) and inherits its DOM-free base: a core
// function is imported by its callers, never forwarded through the app, and a collaborator's method
// is called on the collaborator. The seam lands as named, non-enumerable methods.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { DrawingApp } from '../../../js/core/drawingApp.js';
import { installDelegates } from '../../../js/core/app/delegates.js';
import { EditingBase } from '../../../js/core/app/editing.js';
import { installGestureFlags } from '../../../js/core/pointer/gesture.js';

const JS = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'js');
const own = (cls) => Object.getOwnPropertyNames(cls.prototype).filter((n) => n !== 'constructor');

// The seam is what core/, llm/ and console/ reach the view through; ui/ imports these itself.
const VIEW_SEAM = ['hideSelectionPanels', 'applyLinesListHover', 'showSelectionPanel', 'syncDrawModeUI',
  'updateCoordStatus', 'applyUnitToUI', 'updateButtons', 'updateProjectTitle', 'updateInfo'];

test('the view seam lands on the prototype as named, non-enumerable methods, and nothing else is forwarded', () => {
  for (const name of VIEW_SEAM) {
    const d = Object.getOwnPropertyDescriptor(DrawingApp.prototype, name);
    assert.equal(typeof d?.value, 'function', name);
    assert.equal(d.enumerable, false, `${name} is not enumerable, like a class method`);
    assert.equal(d.value.name, name);
  }
  // Positively: what the prototype holds is its class body, the seam and the gesture flags — nothing else.
  const body = [...fs.readFileSync(path.join(JS, 'core/drawingApp.js'), 'utf8')
    .matchAll(/^ {2}(?:async |get |set |static )?([A-Za-z_$][\w$]*)\s*\([^)]*\)\s*\{/gm)].map((m) => m[1]);
  class Flags {}
  installGestureFlags(Flags.prototype);
  assert.deepEqual(own(DrawingApp).sort(), [...new Set([...body.filter((n) => n !== 'constructor'), ...VIEW_SEAM, ...own(Flags)])].sort());
  for (const name of own(EditingBase)) assert.ok(!VIEW_SEAM.includes(name), `${name} is written out, not forwarded`);
  assert.equal(Object.getPrototypeOf(DrawingApp.prototype), EditingBase.prototype, 'the editing methods are inherited');
  assert.ok(own(EditingBase).every((n) => !Object.hasOwn(DrawingApp.prototype, n)), 'and never shadowed by the app');
});

// A method called on the app is one the app has — its own, inherited, or a member something installs
// on it (`app.quarterTurn = …`); a call left behind on a retired forwarder names neither.
test('every method a module calls on the app is one the app has', () => {
  const names = new Set();
  for (let p = DrawingApp.prototype; p && p !== Object.prototype; p = Object.getPrototypeOf(p)) Object.getOwnPropertyNames(p).forEach((n) => names.add(n));
  const called = new Map(), assigned = new Set();
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) { if (e.name !== 'wasm') walk(p); continue; }
      if (!e.name.endsWith('.js')) continue;
      const src = fs.readFileSync(p, 'utf8').replace(/\/\*[\s\S]*?\*\/|\/\/.*$/gm, '');
      for (const m of src.matchAll(/\b(?:app|this\.app)\??\.(\w+)\??\.?\(/g)) if (!called.has(m[1])) called.set(m[1], path.relative(JS, p));
      for (const m of src.matchAll(/\b(?:app|this\.app|this)\.(\w+)\s*=(?!=)/g)) assigned.add(m[1]);
    }
  };
  walk(JS);
  assert.ok(called.size > 20, 'the scan read the tree');
  assert.deepEqual([...called].filter(([n]) => !names.has(n) && !assigned.has(n)).map(([n, f]) => `${f}: app.${n}()`), []);
});

test('a forwarder hands the app first and returns what its target returns', () => {
  class Host {}
  const seen = [];
  installDelegates(Host.prototype, { probe: (app, ...a) => { seen.push([app, a]); return 'r'; } });
  const h = new Host();
  assert.equal(h.probe(1, 2), 'r');
  assert.deepEqual(seen, [[h, [1, 2]]]);
});

