// Conformance-fixture walker: runs the shared format corpora in js/config/fixtures/
// ({layout, stencilProject, deepLink}) through the REAL browser modules, pinning the
// browser reference behavior the other surfaces mirror (cli/mcp/pystencil/bot/desktop).
// Self-contained on purpose (no helpers/): later per-surface walkers port this file.
// Each corpus dir carries a _schema.md describing its vector shapes.
import { test } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { buildLayoutPayload, sanitizeLines } from '../js/core/layout.js';
import { parseProjectFile, MAX_PROJECT_FILE_CHARS } from '../js/core/project/projectFile.js';
import { normalizeLaunchPayload, encodeTelegramStartPayload, LAUNCH_DATA_URL_MAX } from '../js/core/deepLink.js';

const CONFIG_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'js', 'config');
const FIXTURES = path.join(CONFIG_DIR, 'fixtures');

// Load a family's vectors from every *.json in its dir (sorted for stable test names),
// once per family however many times a test asks for it.
const families = new Map();
const loadFamily = (family) => {
  if (!families.has(family)) families.set(family, readFamily(family));
  return families.get(family);
};

const readFamily = (family) => {
  const dir = path.join(FIXTURES, family);
  const files = fs.readdirSync(dir).filter((f) => f.endsWith('.json')).sort();
  assert.ok(files.length > 0, `no fixture files in ${dir}`);
  return files.flatMap((f) =>
    JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')).map((v) => ({ vector: v, file: f })));
};

// Structural JSON round-trip: drops undefined-valued keys the way serialization would.
const jsonClone = (v) => JSON.parse(JSON.stringify(v));

// { $repeat: { prefix, char, length } } recipes stand in for oversize strings (deepLink/_schema.md). Keys are
// set with defineProperty, so an own "__proto__" fixture key stays an own key instead of rewriting a prototype.
const expand = (v) => {
  if (Array.isArray(v)) return v.map(expand);
  if (v && typeof v === 'object') {
    if (v.$repeat && typeof v.$repeat === 'object') {
      const { prefix = '', char, length } = v.$repeat;
      return prefix + char.repeat(length - prefix.length);
    }
    const out = {};
    for (const k of Object.keys(v)) {
      Object.defineProperty(out, k, { value: expand(v[k]), enumerable: true, writable: true, configurable: true });
    }
    return out;
  }
  return v;
};

// Cross-surface per-line defaults, sourced from the app's own config so the corpus and the browser's
// DEFAULT_VISUALS can never drift apart silently.
const DV = JSON.parse(fs.readFileSync(path.join(CONFIG_DIR, 'constants.json'), 'utf8')).DEFAULT_VISUALS;
const LINE_DEFAULTS = {
  color: DV.color, thickness: DV.thickness, pointSize: DV.pointSize, style: DV.style,
  locked: false, fillColor: 'transparent', pointColor: '',
};
const fillLine = (line) => ({ points: [], ...LINE_DEFAULTS, ...line });

for (const { vector: v, file } of loadFamily('layout')) {
  test(`layout/${file}: ${v.name}`, () => {
    if (v.layout !== undefined) {
      const payload = buildLayoutPayload(v.layout);
      assert.deepStrictEqual(jsonClone(payload), v.expectPayload);
      if (v.expectKeyOrder) {
        assert.deepStrictEqual(Object.keys(jsonClone(payload)), v.expectKeyOrder, 'payload key order');
      }
      for (const k of v.forcedKeys || []) {
        assert.ok(k in payload, `forced key "${k}" present even when its value is undefined`);
      }
      if (v.expectLinesSameRef) {
        assert.strictEqual(payload.lines, v.layout.lines, 'lines pass through by reference, uncopied');
      }
    } else {
      const sanitized = sanitizeLines(expand(v.sparse));
      assert.deepStrictEqual(sanitized, v.expectSanitized, 'sanitizeLines output');
      assert.deepStrictEqual(sanitized.map(fillLine), v.expectFilled, 'defaults-filled lines');
    }
  });
}

// ── stencilProject/ ─────────────────────────────────────────────────────────
for (const { vector: v, file } of loadFamily('stencilProject')) {
  test(`stencilProject/${file}: ${v.name}`, () => {
    assert.ok(v.file.length < MAX_PROJECT_FILE_CHARS / 1024, 'fixture stays far below the size gate');
    const res = parseProjectFile(v.file);
    if (v.expect === 'ok') {
      assert.strictEqual(res.ok, true, `expected ok, got error: ${res.error}`);
      assert.deepStrictEqual(jsonClone(res.project), v.project);
    } else {
      assert.strictEqual(res.ok, false, 'expected a parse error');
      assert.strictEqual(typeof res.error, 'string');
      if (v.errorIncludes) {
        assert.ok(res.error.includes(v.errorIncludes), `"${res.error}" should mention "${v.errorIncludes}"`);
      }
    }
  });
}

// ── deepLink/ ───────────────────────────────────────────────────────────────
for (const { vector: v, file } of loadFamily('deepLink')) {
  test(`deepLink/${file}: ${v.name}`, () => {
    if (v.serverUrl !== undefined) {
      // telegramStart.json: the 3-surface t.me ?start= codec golden vectors.
      assert.strictEqual(encodeTelegramStartPayload(v.serverUrl, v.projectId), v.expectPayload);
      if (v.expectPayload != null) assert.match(v.expectPayload, /^1[A-Za-z0-9_-]+$/, 'telegram-safe charset');
      return;
    }
    const res = normalizeLaunchPayload(expand(v.payload));
    if (v.expect === 'rejected') assert.strictEqual(res, null);
    else assert.deepStrictEqual(res, expand(v.normalized));
  });
}

// ── corpus meta-invariants ──────────────────────────────────────────────────
test('fixture corpus: names unique per family, recipes sized against the real cap', () => {
  for (const family of ['layout', 'stencilProject', 'deepLink']) {
    const names = loadFamily(family).map(({ vector }) => vector.name);
    assert.ok(names.every(Boolean), `${family}: every vector is named`);
    assert.strictEqual(new Set(names).size, names.length, `${family}: duplicate vector names`);
  }
  // $repeat lengths in the corpus are written against a 32 MiB LAUNCH_DATA_URL_MAX;
  // if the cap moves, regenerate the oversize vectors too.
  assert.strictEqual(LAUNCH_DATA_URL_MAX, 32 * 1024 * 1024);
  assert.strictEqual(MAX_PROJECT_FILE_CHARS, 32 * 1024 * 1024);
});

test('fixture corpus: pollution vectors did not touch Object.prototype', () => {
  assert.strictEqual(Object.prototype.polluted, undefined);
  assert.strictEqual({}.p2, undefined);
});
