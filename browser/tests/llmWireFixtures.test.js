// Fixture walker for the LLM conformance corpus in js/config/llm/fixtures/: providerWire/ (contract §6 request
// bodies and reply extraction, llmClient.js), sanitizer/ (provider-error sanitizer vectors) and chatDoc/
// (§12.1 persisted-chat document round-trip and tolerance, chatStore.js). The fixtures are literal JSON and
// other surfaces walk the identical files, so a behaviour change here fails every walker. Self-contained on
// purpose: no tests/helpers imports.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { createLlmClient, sanitizeProviderText, LlmError } from '../js/llm/llmClient.js';
import { parseChatDoc, buildChatDoc, CHAT_DOC_VERSION } from '../js/llm/chatStore.js';

const FIXTURES = fileURLToPath(new URL('../js/config/llm/fixtures/', import.meta.url));

// Every *.json in a family dir is an array of case objects with a unique name; read
// once per family however many times a test asks for it.
const families = new Map();
const loadFamily = (family) => {
  if (!families.has(family)) families.set(family, readFamily(family));
  return families.get(family);
};

const readFamily = (family) => {
  const dir = `${FIXTURES}${family}`;
  const files = readdirSync(dir).filter((f) => f.endsWith('.json')).sort();
  assert.ok(files.length > 0, `${family}: no fixture files`);
  const cases = [];
  for (const file of files) {
    const arr = JSON.parse(readFileSync(`${dir}/${file}`, 'utf8'));
    assert.ok(Array.isArray(arr) && arr.length > 0, `${family}/${file}: expected a non-empty array`);
    for (const c of arr) cases.push({ ...c, file });
  }
  const names = new Set();
  for (const c of cases) {
    assert.equal(typeof c.name, 'string');
    assert.ok(!names.has(c.name), `${family}: duplicate case name "${c.name}"`);
    names.add(c.name);
  }
  return cases;
};

// ── providerWire ─────────────────────────────────────────────────────────────
// fixture "provider" → the §5 provider value the client is configured with.
const PROVIDER_MAP = { ollama: 'ollama', openai: 'openai-compat', server: 'stencil-server' };

test('providerWire: request bodies and reply extraction match the golden vectors', async (t) => {
  for (const fx of loadFamily('providerWire')) {
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
test('sanitizer: provider-error text vectors match sanitizeProviderText', async (t) => {
  for (const fx of loadFamily('sanitizer')) {
    await t.test(`${fx.file}: ${fx.name}`, () => {
      assert.equal(sanitizeProviderText(fx.input), fx.expect);
    });
  }
});

// The corpus itself must uphold the sanitizer's promise: no fixture expectation
// re-introduces what the sanitizer exists to remove.
test('sanitizer: every expectation is URL-free, token-free and ≤ 200 chars', () => {
  for (const fx of loadFamily('sanitizer')) {
    assert.ok(fx.expect.length <= 200, `${fx.name}: over 200 chars`);
    assert.ok(!/[a-z][a-z0-9+.-]*:\/\//i.test(fx.expect), `${fx.name}: URL survived`);
    assert.ok(!/[A-Za-z0-9_-]{24,}/.test(fx.expect), `${fx.name}: token-shaped run survived`);
    assert.ok(!/[\p{Cc}\p{Cf}]/u.test(fx.expect), `${fx.name}: control/format char survived`);
  }
});

// ── chatDoc ──────────────────────────────────────────────────────────────────
test('chatDoc roundtrip: canonical documents are fixed points of parse/serialize', async (t) => {
  const cases = loadFamily('chatDoc').filter((c) => c.file === 'roundtrip.json');
  assert.ok(cases.length > 0);
  for (const fx of cases) {
    await t.test(fx.name, () => {
      assert.equal(fx.doc.version, CHAT_DOC_VERSION);
      const fromString = parseChatDoc(JSON.stringify(fx.doc));
      assert.deepEqual(fromString, fx.doc, 'parse(JSON string) must equal the document');
      const fromObject = parseChatDoc(JSON.parse(JSON.stringify(fx.doc)));
      assert.deepEqual(fromObject, fx.doc, 'parse(object) must equal the document');
      const rebuilt = buildChatDoc(fromString.messages, fromString.savedAt);
      assert.deepEqual(rebuilt, fx.doc, 'serialize(parse(doc)) must equal the document');
    });
  }
});

test('chatDoc tolerance: lenient-read behavior is pinned', async (t) => {
  const cases = loadFamily('chatDoc').filter((c) => c.file === 'tolerance.json');
  assert.ok(cases.length > 0);
  for (const fx of cases) {
    await t.test(fx.name, () => {
      assert.ok(('doc' in fx) !== ('docString' in fx), 'exactly one of doc/docString');
      const input = 'docString' in fx ? fx.docString : JSON.parse(JSON.stringify(fx.doc));
      const parsed = parseChatDoc(input);
      assert.deepEqual(parsed, fx.expectParsed);
      // A tolerated read must itself be canonical: re-parsing what came out
      // changes nothing (the parser normalizes in one pass).
      if (parsed !== null) assert.deepEqual(parseChatDoc(JSON.stringify(parsed)), parsed);
    });
  }
});
