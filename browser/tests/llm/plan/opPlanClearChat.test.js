// §10 clearChat (js/llm/plan.js): the clear-conversation flow deferred to the plan's
// end, its declined-confirm note, and the deferredSink the turn runner flushes.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan, EDITOR_SETTINGS_PROMPT, FORBIDDEN_OPS } from '../../../js/llm/plan/opPlan.js';
import { plan, bad, makeStub, dropsWithWarning } from '../../helpers/opPlanRig.js';

// ── §10 clearChat: the clear-conversation flow, deferred to the plan's end ──

test('clearChat: no fields; dropped from variants and ask previews; sits in the §10 block', () => {
  assert.deepStrictEqual(parseOpPlan(plan({ actions: [{ op: 'clearChat' }] })).actions,
    [{ op: 'clearChat' }]);
  bad({ op: 'clearChat', now: true });                   // no fields at all
  dropsWithWarning(plan({ variants: [{ label: 'v', actions: [{ op: 'clearChat' }] }] }),
    /Dropped variant 1 \("v"\).*editor-settings op "clearChat" is not allowed inside variants/);
  dropsWithWarning(plan({
    ask: { question: 'Q', options: [{ label: 'A', actions: [{ op: 'clearChat' }] }, { label: 'B' }] },
  }), /Dropped the preview for ask option 1 \("A"\).*not allowed inside variants/);
  // Its bullet rides the settings block, after incognito and before the also-lines.
  assert.ok(EDITOR_SETTINGS_PROMPT.indexOf('{"op":"incognito"') < EDITOR_SETTINGS_PROMPT.indexOf('{"op":"clearChat"}'));
  assert.ok(EDITOR_SETTINGS_PROMPT.indexOf('{"op":"clearChat"}') < EDITOR_SETTINGS_PROMPT.indexOf('"removeProject" also accepts'));
});

test('clearChat and the forbidden "chat" toggle name stay disjoint (§13 boundary)', () => {
  assert.ok(!FORBIDDEN_OPS.has('clearChat'), 'clearing the conversation IS drivable');
  assert.ok(FORBIDDEN_OPS.has('chat'), 'the persistence/consent toggle name stays forbidden');
});

test('executor: clearChat defers to the END — later actions and variant renders run first', async () => {
  const { stub, calls } = makeStub();
  const seen = [];
  const p = parseOpPlan(plan({
    actions: [{ op: 'clearChat' }, { op: 'rotate', dir: 'left' }],
    variants: [{ label: 'sepia', actions: [{ op: 'filter', mode: 'sepia' }] }],
  }));
  const { warnings } = await executeOpPlan(p, stub, {
    exportImage: async () => { seen.push('export'); return 'data:,'; },
    // Snapshot what already ran by the time the confirm would show.
    clearChatConversation: async () => { seen.push(['clear', calls.map((c) => c[0])]); return null; },
  });
  const [tag, before] = seen.at(-1);
  assert.equal(tag, 'clear', 'the clear is the very last thing that happened');
  assert.ok(before.includes('rotateLeft'), 'the later top-level action ran first');
  assert.ok(seen.includes('export'), 'the variant rendered first too');
  assert.deepStrictEqual(warnings, []);
});

test('executor: a declined clearChat confirm is a note, never a failed plan', async () => {
  const { stub, calls } = makeStub();
  const p = parseOpPlan(plan({ actions: [{ op: 'clearChat' }, { op: 'units', value: 'cm' }] }));
  const { warnings } = await executeOpPlan(p, stub, {
    clearChatConversation: async () => 'clear canceled',
  });
  assert.deepStrictEqual(calls, [['apply', { unit: 'cm' }]], 'the rest of the plan still ran');
  assert.ok(warnings.some((w) => w.includes('clearChat: clear canceled')));
});

test('executor: clearChat without the capability is a plan error', async () => {
  const { stub } = makeStub();
  await assert.rejects(
    () => executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'clearChat' }] })), stub, {}),
    /cannot clear the conversation/);
});

test('executor: a deferredSink takes clearChat UNEXECUTED for the turn runner to flush later', async () => {
  const { stub, calls } = makeStub();
  const sink = [];
  const { warnings } = await executeOpPlan(
    parseOpPlan(plan({ actions: [{ op: 'clearChat' }, { op: 'units', value: 'cm' }] })), stub, {
      clearChatConversation: async () => { throw new Error('must not run inside this round'); },
      deferredSink: sink,
    });
  assert.deepStrictEqual(sink, [{ op: 'clearChat' }], 'collected, not executed');
  assert.deepStrictEqual(calls, [['apply', { unit: 'cm' }]], 'the rest of the plan ran normally');
  assert.deepStrictEqual(warnings, []);
});
