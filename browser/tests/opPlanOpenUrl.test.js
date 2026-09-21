// §10 openUrl (js/llm/opPlan.js): only a URL the user echoed loads, incognito adopts
// in place, and a fetch failure explains what the editor can reach.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub, dropsWithWarning } from './helpers/opPlanRig.js';

// ── §10 openUrl: user-echoed URLs only ──
test('openUrl: http(s) url + optional incognito; junk fails', () => {
  assert.deepStrictEqual(ok({ op: 'openUrl', url: 'https://a.example/cat.jpg' }), { op: 'openUrl', url: 'https://a.example/cat.jpg' });
  assert.deepStrictEqual(ok({ op: 'openUrl', url: 'http://a.example/x.png', incognito: true }),
    { op: 'openUrl', url: 'http://a.example/x.png', incognito: true });
  bad({ op: 'openUrl' });
  bad({ op: 'openUrl', url: 'ftp://a.example/x' });
  bad({ op: 'openUrl', url: 'not a url' });
  bad({ op: 'openUrl', url: 'https://a.example/x', incognito: 'yes' });
  bad({ op: 'openUrl', url: 'https://a.example/x', extra: 1 });
});

test('openUrl executes ONLY a URL the user typed; incognito rides the injected launcher', async () => {
  const URL_OK = 'https://a.example/cat.jpg';
  const p = parseOpPlan(plan({ actions: [{ op: 'openUrl', url: URL_OK }] }));

  // Not in the user's own words → the whole plan fails with the guard's message.
  const { stub: s1 } = makeStub();
  await assert.rejects(
    () => executeOpPlan(p, s1, { userText: () => 'open something nice' }),
    /not a URL you gave/);

  // Echoed from the user → loads through the facade's own URL path, and the
  // executor reports what it did in its own words (renders with the reply).
  const { stub: s2, calls: c2 } = makeStub();
  const done = await executeOpPlan(p, s2, { userText: () => `please open ${URL_OK} for me` });
  assert.deepStrictEqual(c2, [['load', URL_OK]]);
  assert.ok(done.warnings.some((w) => w.includes(`Loaded ${URL_OK}`)));

  // incognito → the injected adopt-in-place capability (still THIS editor, never a tab).
  const opened = [];
  const inc = parseOpPlan(plan({ actions: [{ op: 'openUrl', url: URL_OK, incognito: true }] }));
  const { stub: s3, calls: c3 } = makeStub();
  const doneInc = await executeOpPlan(inc, s3, { userText: () => URL_OK, openIncognito: async (u) => opened.push(u) });
  assert.deepStrictEqual(opened, [URL_OK]);
  assert.deepStrictEqual(c3, []);
  assert.ok(doneInc.warnings.some((w) => w.includes('fresh incognito editor')));
});

// The bug this fixes: incognito used to hand off to a new tab, so everything after it
// ran here on an EMPTY editor — `crop` threw "No image loaded", killing the turn.
test('the actions after an incognito openUrl act on the picture it just loaded', async () => {
  const URL_OK = 'https://a.example/cat.jpg';
  const p = parseOpPlan(plan({
    actions: [
      { op: 'openUrl', url: URL_OK, incognito: true },
      { op: 'filter', mode: 'bw' },
      { op: 'crop', spec: { aspect: '3:4' } },
      { op: 'compare', mode: 'horizontal' },
    ],
  }));
  // Starts EMPTY, as the editor is before the URL lands — and crops refuse an empty
  // editor the way the real facade does, so a hand-off to another tab would fail here.
  const { stub, calls } = makeStub({ imageSize: undefined });
  const bareCrop = stub.crop;
  stub.crop = (spec) => {
    if (!stub.imageSize) throw new Error('No image loaded to crop');
    return bareCrop(spec);
  };
  const done = await executeOpPlan(p, stub, {
    userText: () => URL_OK,
    openIncognito: async () => { stub.imageSize = { width: 400, height: 300 }; },
  });
  assert.deepStrictEqual(calls, [
    ['apply', { filter: 'bw' }],
    ['crop', { aspect: '3:4' }],
    ['compareMode', 'horizontal'],
  ]);
  assert.ok(!done.warnings.some((w) => /No image loaded/i.test(w)));
});

test('openUrl is editor-scope: its variant is dropped — with the hint in the warning', () => {
  const p = dropsWithWarning(plan({
    variants: [{ label: 'v', actions: [{ op: 'openUrl', url: 'https://a.example/x.jpg' }] }],
  }), /Dropped variant 1 \("v"\).*top-level action.*extension assistant/);
  assert.strictEqual(p.variants.length, 0);
});

test('openUrl load failure explains what the editor CAN fetch instead of the bare TypeError', async () => {
  const URL_OK = 'https://en.example.org/wiki/Cat';
  const p = parseOpPlan(plan({ actions: [{ op: 'openUrl', url: URL_OK }] }));
  const { stub } = makeStub();
  stub.load = async () => { throw new TypeError('Failed to fetch'); };
  await assert.rejects(
    () => executeOpPlan(p, stub, { userText: () => URL_OK }),
    (err) => err.message.includes(URL_OK) && err.message.includes('Failed to fetch')
      && err.message.includes("extension's assistant"));
});
