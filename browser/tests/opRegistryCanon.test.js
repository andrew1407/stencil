// Pins js/config/llm/opRegistry.json to the LIVE browser structures (js/llm/plan.js
// OPS/LIMITS/ASK_LIMITS/FORBIDDEN_OPS) and to the shared op-plan fixture corpus: the
// registry is normative, so it must not drift from the reference it was measured against.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { OPS, LIMITS, ASK_LIMITS, FORBIDDEN_OPS } from '../js/llm/plan/opPlan.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const registry = JSON.parse(readFileSync(
  path.join(HERE, '..', 'js', 'config', 'llm', 'opRegistry.json'), 'utf8'));
const FIXTURES_DIR = path.join(HERE, '..', 'js', 'config', 'llm', 'fixtures', 'opPlan');

const ALL_PROFILES = ['editor', 'console', 'bot', 'mcp', 'extension'];
const entriesByName = (name) => registry.ops.filter((e) => e.name === name);
const entryById = (id) => registry.ops.find((e) => e.id === id);

// ── well-formedness ─────────────────────────────────────────────────────────

test('registry is well-formed: unique ids, known profiles, profile lists ↔ op entries agree', () => {
  const ids = registry.ops.map((e) => e.id);
  assert.equal(new Set(ids).size, ids.length, 'op entry ids must be unique');
  for (const e of registry.ops) {
    assert.ok(typeof e.name === 'string' && e.name, `${e.id}: name required`);
    assert.ok(Array.isArray(e.profiles) && e.profiles.length, `${e.id}: profiles required`);
    for (const p of e.profiles) assert.ok(ALL_PROFILES.includes(p), `${e.id}: unknown profile "${p}"`);
    assert.ok(e.keys && typeof e.keys === 'object', `${e.id}: keys map required`);
    if (e.bulletSharedWith) {
      assert.equal(e.bullet, null, `${e.id}: a shared-bullet entry carries no bullet of its own`);
      assert.ok(entryById(e.bulletSharedWith)?.bullet, `${e.id}: bulletSharedWith names an entry with a bullet`);
    } else {
      assert.ok(typeof e.bullet === 'string' && e.bullet.startsWith('- {'), `${e.id}: bullet required`);
    }
  }
  // profiles.X.ops lists the entry names carrying profile X, by WIRE name.
  for (const p of ALL_PROFILES) {
    const listed = registry.profiles[p].ops;
    assert.equal(new Set(listed).size, listed.length, `profiles.${p}.ops has duplicates`);
    const fromEntries = new Set(registry.ops.filter((e) => e.profiles.includes(p)).map((e) => e.name));
    assert.deepEqual(new Set(listed), fromEntries, `profiles.${p}.ops must equal the entries claiming profile "${p}"`);
  }
});

test('measured per-profile op counts', () => {
  assert.equal(registry.profiles.editor.ops.length, 36);     // browser 35 (incl. voiceChat) + desktop openFile
  assert.equal(registry.profiles.console.ops.length, 23);    // cli 23 ⊇ pystencil 19
  assert.equal(registry.profiles.bot.ops.length, 24);        // 22 bullet groups, undo/redo + connect/disconnect shared
  assert.equal(registry.profiles.mcp.ops.length, 10);
  assert.equal(registry.profiles.extension.ops.length, 12);
});

// `filter` is the one wire name carried twice: the editor op and the extension's panel filter.
test('the registry carries 49 entries under 48 wire names', () => {
  const names = registry.ops.map((e) => e.name);
  assert.equal(names.length, 49);
  assert.deepEqual(names.filter((n, i) => names.indexOf(n) !== i), ['filter']);
});

// ── limits ──────────────────────────────────────────────────────────────────

test('limits match the live browser LIMITS / ASK_LIMITS', () => {
  assert.equal(registry.limits.MAX_ACTIONS, LIMITS.actions);
  assert.equal(registry.limits.MAX_VARIANTS, LIMITS.variants);
  assert.equal(registry.limits.MAX_LAYOUT_LINES, LIMITS.layoutLines);
  assert.equal(registry.limits.MAX_FRAME_INDICES, LIMITS.frameIndices);
  assert.equal(registry.limits.MAX_STRING_CHARS, LIMITS.stringChars);
  assert.equal(registry.limits.MAX_PATH_CHARS, LIMITS.pathChars);
  assert.deepEqual(registry.limits.ask, {
    minOptions: ASK_LIMITS.minOptions, maxOptions: ASK_LIMITS.maxOptions,
    question: ASK_LIMITS.question, label: ASK_LIMITS.label, answer: ASK_LIMITS.answer,
  });
});

test('name/steps caps hold against the live validators (120 / 80 / 20)', () => {
  const ok = (op, a) => OPS[op].validate({ op, ...a });
  const bad = (op, a) => assert.throws(() => OPS[op].validate({ op, ...a }));
  ok('save', { name: 'x'.repeat(registry.limits.MAX_SAVE_NAME) });
  bad('save', { name: 'x'.repeat(registry.limits.MAX_SAVE_NAME + 1) });
  ok('renameProject', { name: 'x'.repeat(registry.limits.MAX_RENAME_NAME) });
  bad('renameProject', { name: 'x'.repeat(registry.limits.MAX_RENAME_NAME + 1) });
  ok('undo', { steps: registry.limits.MAX_UNDO_STEPS });
  bad('undo', { steps: registry.limits.MAX_UNDO_STEPS + 1 });
  ok('redo', { steps: registry.limits.MAX_UNDO_STEPS });
  bad('redo', { steps: registry.limits.MAX_UNDO_STEPS + 1 });
  // save.path (§2.1, Phase-6 reconciliation): ≤ MAX_PATH_CHARS, never a URL.
  ok('save', { path: 'x'.repeat(registry.limits.MAX_PATH_CHARS) });
  bad('save', { path: 'x'.repeat(registry.limits.MAX_PATH_CHARS + 1) });
  bad('save', { path: 'https://example.com/x.png' });
});

// ── regexes: behavioral parity with the live validators ─────────────────────

const validates = (op, a) => { try { OPS[op].validate({ op, ...a }); return true; } catch { return false; } };
const re = (name) => new RegExp(registry.regexes[name]);

test('CROP_TOKEN agrees with the live crop validator', () => {
  const samples = ['10%', '-10%', '.5px', '3cm', '4in', '10', '1.5', '-0.25%', '+5', '5 %', '10pt', '', 'px', '10..5'];
  for (const t of samples) {
    assert.equal(re('CROP_TOKEN').test(t), validates('crop', { spec: { x1: t } }), `CROP_TOKEN vs validator on ${JSON.stringify(t)}`);
  }
});

test('CROP_ASPECT agrees with the live crop validator (plus the >0 check beyond the regex)', () => {
  const samples = ['3:4', '1:1', '10:07', '16:9', '3:4.5', '3-4', ':4', '3:', 'a:b'];
  for (const t of samples) {
    assert.equal(re('CROP_ASPECT').test(t), validates('crop', { spec: { aspect: t } }), `CROP_ASPECT vs validator on ${JSON.stringify(t)}`);
  }
  // Positivity is part of the regex itself (portable to the hand-written matchers).
  assert.equal(re('CROP_ASPECT').test('0:4'), false);
  assert.equal(re('CROP_ASPECT').test('10:07'), true);
  assert.equal(validates('crop', { spec: { aspect: '0:4' } }), false);
});

test('PAGE_FORMAT agrees with the live page validator', () => {
  const samples = ['a0', 'a4', 'a10', 'b7', 'c10', 'a11', 'd4', 'A4', 'a', 'c11'];
  for (const t of samples) {
    assert.equal(re('PAGE_FORMAT').test(t), validates('page', { format: t }), `PAGE_FORMAT vs validator on ${JSON.stringify(t)}`);
  }
});

test('HEX agrees with the live filter-tint validator', () => {
  const samples = ['#aabbcc', '#AABBCC', '#123456', '#abc', 'aabbcc', '#aabbcg', '#aabbccdd'];
  for (const t of samples) {
    assert.equal(re('HEX').test(t), validates('filter', { mode: 'custom', tint: t }), `HEX vs validator on ${JSON.stringify(t)}`);
  }
});

test('CSS_NAME agrees with the live blank-color validator (on non-hex colors)', () => {
  const samples = ['white', 'CornflowerBlue', 'not a name', 'rgb(0,0,0)', ''];
  for (const t of samples) {
    assert.equal(re('CSS_NAME').test(t), validates('blank', { color: t }), `CSS_NAME vs validator on ${JSON.stringify(t)}`);
  }
});

test('FORMULA_X / FORMULA_Y agree with the live formula validator (non-empty exprs)', () => {
  const samples = ['x*2+10', 'x**2', '(x+1)/2', 'x^2', 'x + y', '2**3', 'abs(x)'];
  for (const t of samples) {
    assert.equal(re('FORMULA_X').test(t), validates('formula', { axis: 'x', expr: t }), `FORMULA_X vs validator on ${JSON.stringify(t)}`);
    const ty = t.replace(/x/g, 'y');
    assert.equal(re('FORMULA_Y').test(ty), validates('formula', { axis: 'y', expr: ty }), `FORMULA_Y vs validator on ${JSON.stringify(ty)}`);
  }
});
