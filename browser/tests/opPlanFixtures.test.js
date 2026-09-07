// Walks the shared op-plan conformance corpus (js/config/llm/fixtures/opPlan/) against
// the REAL browser validator. Every surface re-implements the same plan grammar; the
// corpus is the cross-language safety net — this file is the reference walker the other
// surfaces' walkers copy (fixture format: _schema.md in the fixtures dir).
//
// Verdict semantics: "valid" = parseOpPlan returns (a chat-only fallback counts as
// valid), "invalid" = it throws. An object `input` is serialized and fed to the parser
// exactly as a model reply would arrive; a string `input` is fed verbatim (fence
// stripping / JSON extraction / duplicate-key cases). The browser asserts fixtures
// whose profiles include "editor" or "all", honouring a knownDivergence.browser
// override where a cross-surface disagreement has been measured and pinned.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { parseOpPlan } from '../js/llm/opPlan.js';

const FIXTURES_DIR = path.join(
  path.dirname(fileURLToPath(import.meta.url)), '..', 'js', 'config', 'llm', 'fixtures', 'opPlan');

const PROFILES = new Set(['editor', 'console', 'bot', 'mcp', 'extension', 'all']);
const SURFACES = new Set(['browser', 'desktop', 'cli', 'pystencil', 'bot', 'mcp', 'extension']);
const BROWSER_PROFILES = ['editor', 'all'];

const files = readdirSync(FIXTURES_DIR).filter((f) => f.endsWith('.json')).sort();
// The hand-written cases, plus the registry-generated bundle (generated/cases.json —
// tools/genOpPlanFixtures.mjs), each generated case walking as `<name>.json`.
const GENERATED = path.join(FIXTURES_DIR, 'generated', 'cases.json');
const fixtures = [
  ...files.map((file) => ({ file, fx: JSON.parse(readFileSync(path.join(FIXTURES_DIR, file), 'utf8')) })),
  ...JSON.parse(readFileSync(GENERATED, 'utf8')).cases.map((fx) => ({ file: `${fx.name}.json`, fx })),
];

test('the generated bundle is fresh against the registry (npm run gen-fixtures)', async () => {
  const { generate, render } = await import('../tools/genOpPlanFixtures.mjs');
  const registry = JSON.parse(readFileSync(path.join(FIXTURES_DIR, '..', '..', 'opRegistry.json'), 'utf8'));
  assert.equal(readFileSync(GENERATED, 'utf8'), render(generate(registry)), 'generated/cases.json is stale — run `npm run gen-fixtures`');
});

test('the corpus exists and is well-formed', () => {
  assert.ok(fixtures.length >= 300, `expected a real corpus, found ${fixtures.length} fixtures`);
  for (const { file, fx } of fixtures) {
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
