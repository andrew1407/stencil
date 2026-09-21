// The editor profile against the live browser OPS, and the corpus cross-check: the op-plan
// fixtures are the measured membership truth. Split from opRegistryCanon.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { OPS, FORBIDDEN_OPS } from '../js/llm/plan/opPlan.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const registry = JSON.parse(readFileSync(
  path.join(HERE, '..', 'js', 'config', 'llm', 'opRegistry.json'), 'utf8'));
const FIXTURES_DIR = path.join(HERE, '..', 'js', 'config', 'llm', 'fixtures', 'opPlan');

const ALL_PROFILES = ['editor', 'console', 'bot', 'mcp', 'extension'];
const entriesByName = (name) => registry.ops.filter((e) => e.name === name);

// ── editor profile ↔ live OPS ───────────────────────────────────────────────

test('editor profile matches the live browser OPS keys, with drift entries note-marked', () => {
  const editorOps = registry.profiles.editor.ops;
  const notes = registry.profiles.editor.notes || {};
  for (const name of Object.keys(OPS)) {
    assert.ok(editorOps.includes(name), `browser op "${name}" missing from the editor profile`);
  }
  for (const name of editorOps) {
    if (!(name in OPS)) {
      assert.ok(notes[name], `editor op "${name}" is not in the browser OPS — it must carry a drift note`);
    }
  }
  assert.equal(Object.keys(OPS).length, 35, 'the browser registers 35 ops today');
});

test('browser-baselined entries carry the live bullets, also-lines, flags and requires', () => {
  for (const e of registry.ops) {
    if (e.baseline !== 'browser') continue;
    const def = OPS[e.name];
    assert.ok(def, `${e.id}: baseline "browser" but no live OPS entry`);
    if (def.bullet == null) {
      assert.equal(e.bullet, null, `${e.id}: live op has no bullet (shared) — registry must record null`);
      assert.ok(e.bulletSharedWith, `${e.id}: shared-bullet entries name their sibling`);
    } else {
      assert.equal(e.bullet, def.bullet, `${e.id}: bullet drifted from the live browser registry`);
    }
    assert.equal(e.also ?? undefined, def.also, `${e.id}: "also" line drifted`);
    assert.equal(e.alsoOrder ?? undefined, def.alsoOrder, `${e.id}: alsoOrder drifted`);
    assert.deepEqual(e.requires ?? undefined, def.requires, `${e.id}: requires drifted`);
    for (const f of ['editorSetting', 'topLevelOnly', 'newFrame', 'deferred']) {
      assert.equal(!!(e.flags || {})[f], !!def[f], `${e.id}: flag "${f}" drifted`);
    }
  }
});

test('declared keys are recognized by the live validators; a canary key is rejected', () => {
  const UNKNOWN = /unknown field/;
  for (const e of registry.ops) {
    if (e.baseline !== 'browser') continue;
    for (const key of Object.keys(e.keys)) {
      // A documented key must fail on TYPE/shape (or pass), never as `unknown field`.
      let msg = null;
      try { OPS[e.name].validate({ op: e.name, [key]: {} }); } catch (err) { msg = err.message; }
      assert.ok(msg === null || !(UNKNOWN.test(msg) && msg.includes(`"${key}"`)),
        `${e.id}: documented key "${key}" is unknown to the live validator (${msg})`);
    }
    assert.throws(() => OPS[e.name].validate({ op: e.name, zzzCanary: 1 }), UNKNOWN,
      `${e.id}: the live validator must reject undocumented fields`);
  }
});

test('forbidden.core and forbidden.perSurface.browser match the live FORBIDDEN_OPS', () => {
  assert.deepEqual(new Set(registry.forbidden.core), FORBIDDEN_OPS);
  assert.deepEqual(new Set(registry.forbidden.perSurface.browser), FORBIDDEN_OPS);
  // No registered op may sit on any surface's forbidden list FOR that surface's profiles.
  const surfaceProfiles = registry.$meta.surfaceProfiles;
  for (const [surface, names] of Object.entries(registry.forbidden.perSurface)) {
    const profile = surfaceProfiles[surface];
    for (const e of registry.ops) {
      if (!e.profiles.includes(profile)) continue;
      assert.ok(!names.includes(e.name),
        `${e.id}: registered for profile "${profile}" but forbidden on its surface "${surface}"`);
    }
  }
});

// ── corpus cross-check: the fixtures are the measured membership truth ──────

const fixtures = [
  ...readdirSync(FIXTURES_DIR).filter((f) => f.endsWith('.json')).sort()
    .map((file) => ({ file, fx: JSON.parse(readFileSync(path.join(FIXTURES_DIR, file), 'utf8')) })),
  // The registry-generated bundle counts as measured membership too.
  ...JSON.parse(readFileSync(path.join(FIXTURES_DIR, 'generated', 'cases.json'), 'utf8')).cases
    .map((fx) => ({ file: `${fx.name}.json`, fx })),
];

const expandProfiles = (profiles) => (profiles.includes('all') ? ALL_PROFILES : profiles);
const fixtureOps = (input) => {
  if (typeof input !== 'object' || input == null) return [];
  const ops = [];
  for (const a of input.actions || []) if (a && typeof a.op === 'string') ops.push(a.op);
  for (const v of Array.isArray(input.variants) ? input.variants : []) {
    for (const a of (v && v.actions) || []) if (a && typeof a.op === 'string') ops.push(a.op);
  }
  return ops;
};

// Happy-path fixtures that deliberately exercise the §1 unknown-op SKIP of an op
// registered elsewhere — membership legitimately differs there.
const UNKNOWN_OP_SKIP_FIXTURES = new Set(['054-reset-unknown-in-editor.json']);

test('every op a profile-restricted happy-path fixture names is registered for those profiles', () => {
  for (const { file, fx } of fixtures) {
    if (fx.expect !== 'valid' || fx.profiles.includes('all')) continue;
    if (UNKNOWN_OP_SKIP_FIXTURES.has(file)) continue;
    for (const op of fixtureOps(fx.input)) {
      const entries = entriesByName(op);
      if (!entries.length) continue;   // forbidden/unknown-op corpus names — not registry ops
      for (const profile of expandProfiles(fx.profiles)) {
        assert.ok(entries.some((e) => e.profiles.includes(profile)),
          `${file}: names "${op}" for profile "${profile}", but no registry entry registers it there`);
      }
    }
  }
});

test('every registry (op × profile) pair is corroborated by at least one happy-path fixture', () => {
  for (const e of registry.ops) {
    for (const profile of e.profiles) {
      const hit = fixtures.some(({ fx }) => fx.expect === 'valid'
        && expandProfiles(fx.profiles).includes(profile)
        && fixtureOps(fx.input).includes(e.name));
      assert.ok(hit, `${e.id}: profile "${profile}" has no valid fixture exercising it — membership is unmeasured`);
    }
  }
});

test('the regex strings compile', () => {
  for (const [name, source] of Object.entries(registry.regexes)) {
    if (name === 'note') continue;
    assert.doesNotThrow(() => new RegExp(source), `regexes.${name} must compile`);
  }
});
