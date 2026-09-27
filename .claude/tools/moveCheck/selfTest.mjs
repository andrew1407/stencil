// moveCheck's self-test (`node .claude/tools/moveCheck.mjs --self-test`): the unit diff and the
// string-aware scanner, asserted on small sources in every language the scanner knows.

import { normalize, stripComments } from './scan.mjs';
import { diffUnits, unitsOf } from './units.mjs';

// ── self-test ───────────────────────────────────────────────────
export const selfTest = async () => {
  const { strict: assert } = await import('node:assert');
  const one = (file, src) => unitsOf(file, src);
  const bare = (src, lang) => stripComments(src, lang).trimEnd();   // a comment leaves blanks

  // A body that only moved file (and gained a comment) is the same body.
  const before = one('a.js', 'export const add = (a, b) => {\n  return a + b;\n};\n');
  const after = one('b.js', '// moved here\nexport const add = (a, b) => {\n  return a + b; // sum\n};\n');
  assert.equal(before.length, 1);
  assert.deepEqual(diffUnits(before, after), { lost: [], gained: [], unchanged: 1 });

  // An edited body is LOST + NEW.
  const edited = one('b.js', 'export const add = (a, b) => {\n  return a - b;\n};\n');
  const d = diffUnits(before, edited);
  assert.equal(d.lost.length, 1);
  assert.equal(d.gained.length, 1);
  assert.equal(d.unchanged, 0);

  // A `//` inside a string, a template and a regex is content, not a comment.
  assert.equal(stripComments('const u = "a // b";', 'js'), 'const u = "a // b";');
  assert.equal(stripComments('const u = `a // ${x ? "y // z" : `n // ${q}`} b`;', 'js'),
    'const u = `a // ${x ? "y // z" : `n // ${q}`} b`;');
  assert.equal(bare('const r = /a\\/\\/b/.test(s); // gone', 'js'), 'const r = /a\\/\\/b/.test(s);');
  assert.match(stripComments('const q = a / b; // gone\nconst w = 1;', 'js'), /^const q = a \/ b; +\nconst w = 1;$/);

  // Whitespace and comments are noise; the string's own spacing is not.
  assert.equal(normalize('f(  1,\n  2 ); /* c */', 'js'), 'f( 1, 2 );');
  assert.notEqual(normalize('g("a  b");', 'js'), normalize('g("a b");', 'js'));

  // Class methods and expression-bodied arrows are units too.
  const cls = one('c.js', 'class R {\n  #n = 1;\n  draw(ctx) {\n    ctx.fill();\n  }\n  static of(x) { return new R(x); }\n}\nexport const pick = (l) => (l.a ? l.a : l.b);\n');
  assert.deepEqual(cls.map((u) => u.name), ['R.draw', 'R.of', 'pick']);
  assert.deepEqual(diffUnits(cls, one('d.js', 'export const pick = (l) => (l.a ? l.a : l.b);\nclass R {\n  draw(ctx) { ctx.fill(); }\n  static of(x) { return new R(x); }\n}\n')),
    { lost: [], gained: [], unchanged: 3 });

  // C-family: block comments (nested in Rust), raw/verbatim strings, Zig multiline text.
  assert.equal(normalize('int a; /* x\ny */ int b;', 'cpp'), 'int a; int b;');
  assert.equal(bare('let s = r#"a // b"#; // gone', 'rs'), 'let s = r#"a // b"#;');
  assert.equal(normalize('let a = 1; /* x /* y */ still */ let b = 2;', 'rs'), 'let a = 1; let b = 2;');
  assert.equal(bare("let t: &'static str = \"// no\"; // gone", 'rs'), "let t: &'static str = \"// no\";");
  assert.equal(bare('const s = `a // b`; // gone', 'go'), 'const s = `a // b`;');
  assert.equal(bare('const s = "a // b"; // gone', 'zig'), 'const s = "a // b";');
  assert.equal(bare('var s = @"a // b"; // gone', 'cs'), 'var s = @"a // b";');
  assert.equal(bare('auto n = 1\'000\'000; // gone', 'cpp'), 'auto n = 1\'000\'000;');

  console.log('moveCheck self-test: ok');
};
