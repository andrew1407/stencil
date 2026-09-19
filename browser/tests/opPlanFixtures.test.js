// Walks the shared op-plan conformance corpus (js/config/llm/fixtures/opPlan/) against the REAL browser
// validator, and is the reference walker the other surfaces' walkers copy (fixture format: _schema.md).
// Verdict semantics: "valid" means parseOpPlan returns, a chat-only fallback included, "invalid" that it
// throws; an object `input` is serialized as a model reply would arrive, a string `input` fed verbatim. The
// browser asserts fixtures whose profiles include "editor" or "all", honouring knownDivergence.browser.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { parseOpPlan } from '../js/llm/opPlan.js';

const FIXTURES_DIR = path.join(
  path.dirname(fileURLToPath(import.meta.url)), '..', 'js', 'config', 'llm', 'fixtures', 'opPlan');

const PROFILES = new Set(['editor', 'console', 'bot', 'mcp', 'extension', 'all']);
const SURFACES = new Set(['browser', 'desktop', 'cli', 'pystencil', 'bot', 'mcp', 'extension']);
const BROWSER_PROFILES = ['editor', 'all'];

const read = (...rel) => readFileSync(path.join(FIXTURES_DIR, ...rel), 'utf8');
// Two bundles: the hand-written cases (cases.json, each carrying its stable `file` label) and the
// registry-generated ones (generated/cases.json — tools/genOpPlanFixtures.mjs), which walk as `<name>.json`.
const generatedText = read('generated', 'cases.json');
const hand = JSON.parse(read('cases.json')).cases;
const generated = JSON.parse(generatedText).cases;
const fixtures = [
  ...hand.map((fx) => ({ file: fx.file, fx })),
  ...generated.map((fx) => ({ file: `${fx.name}.json`, fx })),
];

// Two digests instead of re-deriving the 211 KB bundle on every run (~10 ms → ~0.4 ms);
// generated/freshness.json is written by `npm run gen-fixtures`.
test('the generated bundle is fresh against the registry (npm run gen-fixtures)', () => {
  const stored = JSON.parse(read('generated', 'freshness.json'));
  const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
  assert.equal(sha256(readFileSync(path.join(FIXTURES_DIR, '..', '..', 'opRegistry.json'))),
    stored.registry, 'opRegistry.json changed without a regen — run `npm run gen-fixtures`');
  assert.equal(sha256(generatedText), stored.cases,
    'generated/cases.json was edited by hand — run `npm run gen-fixtures`');
});

// Floors per bundle, not on the total: the 444 generated cases alone clear any combined
// floor, so a vanished cases.json would otherwise walk green.
test('the corpus exists and is well-formed', () => {
  assert.ok(hand.length >= 180, `hand-written cases.json collapsed to ${hand.length}`);
  assert.ok(generated.length >= 400, `generated/cases.json collapsed to ${generated.length}`);
  for (const { file, fx } of fixtures) {
    assert.equal(typeof file, 'string', `${fx.name}: every case carries a label`);
    assert.equal(`${fx.name}.json`, file.replace(/^\d+-/, ''), `${file}: "name" must match the filename slug`);
    assert.ok(Array.isArray(fx.profiles) && fx.profiles.length, `${file}: "profiles" must be a non-empty array`);
    for (const p of fx.profiles) assert.ok(PROFILES.has(p), `${file}: unknown profile "${p}"`);
    assert.ok(fx.expect === 'valid' || fx.expect === 'invalid', `${file}: "expect" must be valid|invalid`);
    assert.ok(fx.input != null, `${file}: "input" is required`);
    if (fx.expect === 'invalid') assert.ok(typeof fx.reason === 'string' && fx.reason.length, `${file}: invalid cases need a "reason"`);
    for (const [surface, verdict] of Object.entries(fx.knownDivergence || {})) {
      assert.ok(SURFACES.has(surface), `${file}: unknown knownDivergence surface "${surface}"`);
      assert.ok(verdict === 'valid' || verdict === 'invalid', `${file}: knownDivergence verdicts are valid|invalid`);
    }
  }
});

for (const { file, fx } of fixtures) {
  if (!fx.profiles.some((p) => BROWSER_PROFILES.includes(p))) continue;
  const want = fx.knownDivergence?.browser ?? fx.expect;
  test(`${file} → ${want}`, () => {
    const text = typeof fx.input === 'string' ? fx.input : JSON.stringify(fx.input);
    if (want === 'valid') {
      const plan = parseOpPlan(text);   // must not throw
      assert.ok(plan && typeof plan.reply === 'string' && Array.isArray(plan.actions),
        `${file}: a valid parse returns a plan`);
    } else {
      assert.throws(() => parseOpPlan(text), `${file}: expected the browser validator to reject this plan`);
    }
  });
}
