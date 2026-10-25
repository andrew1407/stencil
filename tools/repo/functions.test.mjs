// The function walker caps.mjs holds the 80-line cap with (functions.mjs), over each language's forms.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { functionsOf, overCap } from './functions.mjs';
import { functionBreaches } from '../caps.mjs';

const body = (n) => Array.from({ length: n }, (_, i) => `  x${i}();`).join('\n');
const summary = (file, src) => functionsOf(file, src).map((f) => [f.name, f.lines]);

test('JS: declarations, arrow consts, methods; a nested function counts toward its host', () => {
  const src = `export function a() {\n${body(3)}\n}\nconst b = async (x) => {\n  const inner = () => { y(); };\n${body(2)}\n};\n`
    + `class C {\n  m(p) {\n${body(1)}\n  }\n  get g() { return 1; }\n}\nif (q) {\n  z();\n}\nconst o = { k: 1 };\n`;
  assert.deepEqual(summary('a.js', src), [['a', 5], ['b', 5], ['m', 3], ['g', 1]]);
});

test('C++, Go, Rust, Zig, C#: function bodies, not blocks, classes or initializers', () => {
  assert.deepEqual(summary('a.cpp', `namespace n {\n  void A::f(int x) const {\n${body(2)}\n  }\n  struct S { int v; };\n}\n`), [['A::f', 4]]);
  assert.deepEqual(summary('a.go', `func (s *T) Run(a int) (int, error) {\n${body(2)}\n}\n`), [['Run', 4]]);
  assert.deepEqual(summary('a.rs', `fn go(x: u8) -> Result<(), E> {\n${body(1)}\n}\n`), [['go', 3]]);
  assert.deepEqual(summary('a.zig', `pub fn run(a: u8) !void {\n    if (a > 1) {\n        x();\n    }\n}\n`), [['run', 5]]);
  assert.deepEqual(summary('a.cs', `class K {\n  public int Sum(int a) {\n${body(2)}\n  }\n}\n`), [['Sum', 4]]);
});

test('braces in comments and strings never open a body', () => {
  assert.deepEqual(summary('a.js', `function f() {\n  const s = '}{';\n  // }\n  return s;\n}\n`), [['f', 5]]);
});

test('over the cap: keyed by file and name, a repeat numbered', () => {
  const src = `function f() {\n${body(85)}\n}\nfunction f() {\n${body(90)}\n}\n`;
  assert.deepEqual(overCap('x.js', src).map((o) => [o.key, o.lines]), [['x.js#f', 87], ['x.js#f~2', 92]]);
});

test('the ratchet: a new long function, one past its entry, and a stale entry are breaches', () => {
  const long = `function f() {\n${body(90)}\n}\n`;
  const kinds = (frozen) => functionBreaches(['x.js'], [long], frozen).map((b) => [b.where, b.limit]);
  assert.deepEqual(kinds({}), [['x.js#f', 80]]);
  assert.deepEqual(kinds({ 'x.js#f': 92 }), []);
  assert.deepEqual(kinds({ 'x.js#f': 91 }), [['x.js#f', 'its frozen 91']]);
  assert.deepEqual(functionBreaches(['x.js'], ['function f() {}\n'], { 'x.js#f': 120 }).map((b) => b.value), ['under the cap']);
});
