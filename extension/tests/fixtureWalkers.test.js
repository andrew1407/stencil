// Extension walkers over the SHARED fixture corpus in browser/js/config/ — the
// cross-surface conformance safety net (each family's _schema.md documents its
// format). Node-only: the shipped extension never reads browser/, its tests may.
// This phase PINS current behavior; measured divergences live in
// tests/fixtureOverrides.json, never as production edits.
//
// Families walked here:
//   opPlan       — profile "extension", context {listingLength: 8, tabsLength: 4}
//   providerWire — §6 wire vectors against src/llm/llmClient.js
//   sanitizer    — sanitizeProviderText vectors (byte-identical to the browser's)
//   deepLink     — telegramStart vectors against src/lib/openIn.js
// Families with NO extension implementation, skipped by design:
//   chatDoc          — the extension's chat history is in-memory only (no chatStore)
//   deepLink/launchPayload — normalizeLaunchPayload is the RECEIVING side; the
//                      extension only constructs #stencil= payloads
//   layout           — no buildLayoutPayload/sanitizeLines port in the extension
//   stencilProject   — the extension never parses .stencil files
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import { parseOpPlan } from '../src/llm/opPlan.js';
import { createLlmClient, sanitizeProviderText, LlmError } from '../src/llm/llmClient.js';
import { encodeTelegramStartPayload } from '../src/lib/openIn.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const LLM_FIXTURES = path.join(HERE, '..', '..', 'browser', 'js', 'config', 'llm', 'fixtures');
const CORE_FIXTURES = path.join(HERE, '..', '..', 'browser', 'js', 'config', 'fixtures');
const OVERRIDES = JSON.parse(readFileSync(path.join(HERE, 'fixtureOverrides.json'), 'utf8'));

// The extension parser's fixed measurement context (opPlan/_schema.md).
const CONTEXT = { listingLength: 8, tabsLength: 4 };

// ── opPlan ───────────────────────────────────────────────────────────────────
const PROFILES = new Set(['editor', 'console', 'bot', 'mcp', 'extension', 'all']);
const SURFACES = new Set(['browser', 'desktop', 'cli', 'pystencil', 'bot', 'mcp', 'extension']);

const OP_PLAN_DIR = path.join(LLM_FIXTURES, 'opPlan');
const opPlanBundle = (...rel) => JSON.parse(readFileSync(path.join(OP_PLAN_DIR, ...rel), 'utf8')).cases;
// The hand-written bundle (each case carrying its stable `file` label) plus the
// registry-generated one (browser/tools/genOpPlanFixtures.mjs), which walks as `<name>.json`.
const opPlanHand = opPlanBundle('cases.json');
const opPlanGenerated = opPlanBundle('generated', 'cases.json');
const opPlanFixtures = [
  ...opPlanHand.map((fx) => ({ file: fx.file, fx })),
  ...opPlanGenerated.map((fx) => ({ file: `${fx.name}.json`, fx })),
];

// Corpus-shape check, ported from browser/tests/opPlanFixtures.test.js.
// Floors per bundle, not on the total: the 444 generated cases alone clear any combined
// floor, so a vanished cases.json would otherwise walk green.
test('opPlan: the corpus exists and is well-formed', () => {
  assert.ok(opPlanHand.length >= 180, `hand-written cases.json collapsed to ${opPlanHand.length}`);
  assert.ok(opPlanGenerated.length >= 400, `generated/cases.json collapsed to ${opPlanGenerated.length}`);
  for (const { file, fx } of opPlanFixtures) {
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

test('opPlan: every override names a real fixture', () => {
  const names = new Set(opPlanFixtures.map(({ fx }) => fx.name));
  for (const name of Object.keys(OVERRIDES.opPlan)) assert.ok(names.has(name), `stale override "${name}"`);
});

for (const { file, fx } of opPlanFixtures) {
  if (!fx.profiles.some((p) => p === 'extension' || p === 'all')) continue;
  // Verdict precedence: local override ?? knownDivergence.extension ?? expect.
  const want = OVERRIDES.opPlan[fx.name] ?? fx.knownDivergence?.extension ?? fx.expect;
  test(`opPlan: ${file} → ${want}`, () => {
    const text = typeof fx.input === 'string' ? fx.input : JSON.stringify(fx.input);
    if (want === 'valid') {
      const plan = parseOpPlan(text, CONTEXT);   // must not throw
      assert.ok(plan && typeof plan.reply === 'string' && Array.isArray(plan.actions),
        `${file}: a valid parse returns a plan`);
    } else {
      assert.throws(() => parseOpPlan(text, CONTEXT), `${file}: expected the extension validator to reject this plan`);
    }
  });
}

// ── shared family loader (providerWire/sanitizer array-of-cases files) ───────
const familyCache = new Map();
const loadFamily = (dir) => {
  if (!familyCache.has(dir)) familyCache.set(dir, readFamily(dir));
  return familyCache.get(dir);
};

const readFamily = (dir) => {
  const files = readdirSync(dir).filter((f) => f.endsWith('.json')).sort();
  assert.ok(files.length > 0, `${dir}: no fixture files`);
  const cases = [];
  for (const file of files) {
    const arr = JSON.parse(readFileSync(path.join(dir, file), 'utf8'));
    assert.ok(Array.isArray(arr) && arr.length > 0, `${file}: expected a non-empty array`);
    for (const c of arr) cases.push({ ...c, file });
  }
  const names = new Set();
  for (const c of cases) {
    assert.equal(typeof c.name, 'string');
    assert.ok(!names.has(c.name), `duplicate case name "${c.name}"`);
    names.add(c.name);
  }
  return cases;
};

// ── providerWire ─────────────────────────────────────────────────────────────
// fixture "provider" → the §5 provider value the client is configured with.
const PROVIDER_MAP = { ollama: 'ollama', openai: 'openai-compat', server: 'stencil-server' };

test('providerWire: request bodies and reply extraction match the golden vectors', async (t) => {
  for (const fx of loadFamily(path.join(LLM_FIXTURES, 'providerWire'))) {
    await t.test(`${fx.file}: ${fx.name}`, async () => {
      assert.ok(PROVIDER_MAP[fx.provider], `unknown provider "${fx.provider}"`);
      const captured = {};
      const fetchImpl = async (url, init) => {
        captured.url = url;
        captured.headers = init.headers || {};
        captured.method = init.method;
        captured.body = JSON.parse(init.body);
        if (fx.errorResponse) {
          const b = fx.errorResponse.body;
          return {
            ok: false,
            status: fx.errorResponse.status,
            // resp.json() rejects on a non-JSON body, exactly like fetch's.
            json: async () => JSON.parse(typeof b === 'string' ? b : JSON.stringify(b)),
          };
        }
        return { ok: true, json: async () => JSON.parse(JSON.stringify(fx.response)) };
      };
      const client = createLlmClient({
        settings: { ...fx.settings, provider: PROVIDER_MAP[fx.provider] },
        fetchImpl,
        getToken: () => fx.token ?? '',
      });

      let reply; let error;
      try {
        reply = await client.chat({ system: fx.chat.system, messages: fx.chat.messages });
      } catch (err) {
        if (!(err instanceof LlmError)) throw err;
        error = err;
      }

      // The request the builder produced — exact URL, auth, and body.
      assert.equal(captured.method, 'POST');
      assert.equal(captured.headers['Content-Type'], 'application/json');
      assert.equal(captured.url, fx.expectUrl);
      assert.equal(captured.headers.Authorization, fx.expectAuthorization);
      assert.deepEqual(captured.body, fx.expectBody);

      // The response handling — extracted reply or typed error.
      if (fx.expectError) {
        assert.ok(error, `expected LlmError(${fx.expectError.kind}), got reply ${JSON.stringify(reply)}`);
        assert.equal(error.kind, fx.expectError.kind);
        assert.equal(error.message, fx.expectError.message);
        if ('status' in fx.expectError) assert.equal(error.status, fx.expectError.status);
      } else {
        assert.equal(error, undefined, `unexpected LlmError: ${error?.kind} ${error?.message}`);
        assert.equal(reply, fx.expectReply);
      }
    });
  }
});

// ── sanitizer ────────────────────────────────────────────────────────────────
// The extension's sanitizer mirrors the browser's byte-for-byte (same regexes,
// same UTF-16 caps), so every vector — DIVERGENCE(server,…) ones included —
// matches the literal browser expectation.
test('sanitizer: provider-error text vectors match sanitizeProviderText', async (t) => {
  for (const fx of loadFamily(path.join(LLM_FIXTURES, 'sanitizer'))) {
    await t.test(`${fx.file}: ${fx.name}`, () => {
      assert.equal(sanitizeProviderText(fx.input), fx.expect);
    });
  }
});

// ── deepLink: telegramStart ──────────────────────────────────────────────────
// The extension BUILDS t.me start payloads (src/lib/openIn.js); the shared
// vectors pin its codec byte-compatible with browser/desktop/bot.
test('deepLink: telegramStart vectors match encodeTelegramStartPayload', async (t) => {
  const cases = JSON.parse(readFileSync(path.join(CORE_FIXTURES, 'deepLink', 'telegramStart.json'), 'utf8'));
  assert.ok(Array.isArray(cases) && cases.length > 0);
  for (const fx of cases) {
    await t.test(fx.name, () => {
      assert.equal(encodeTelegramStartPayload(fx.serverUrl, fx.projectId), fx.expectPayload);
    });
  }
});
