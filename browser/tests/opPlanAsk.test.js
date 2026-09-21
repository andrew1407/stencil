// §11 interactive replies (js/llm/plan.js): the `ask` card's parse defaults, its
// option references, the malformed-card errors and askAnswerText.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  parseOpPlan, validateAsk, askAnswerText, ASK_LIMITS, DEFAULT_CUSTOM_LABEL,
} from '../js/llm/plan/opPlan.js';
import { askPlan, askOf } from './helpers/opPlanRig.js';

// ── §11 interactive replies (`ask`) ─────────────────────────────────────────

test('ask: a plain single-choice card parses with its defaults filled in', () => {
  const a = askOf({ question: 'Which tint?', options: [{ label: 'Sepia' }, { label: 'B&W' }] });
  assert.equal(a.question, 'Which tint?');
  assert.equal(a.mode, 'single');              // default
  assert.equal(a.allowCustom, false);          // default
  assert.equal(a.customLabel, DEFAULT_CUSTOM_LABEL);
  assert.deepEqual(a.options.map((o) => o.label), ['Sepia', 'B&W']);
});

test('ask: absent on an ordinary plan, and on a chat-only reply', () => {
  assert.equal(parseOpPlan('{"version":1,"reply":"hi","actions":[]}').ask, null);
  assert.equal(parseOpPlan('just chatting').ask, null);
});

test('ask: multi mode, custom row, and trimmed strings', () => {
  const a = askOf({ question: '  Which ones?  ', mode: 'multi', allowCustom: true, customLabel: ' Other ',
    options: [{ label: ' A ' }, { label: 'B' }] });
  assert.equal(a.question, 'Which ones?');
  assert.equal(a.mode, 'multi');
  assert.equal(a.allowCustom, true);
  assert.equal(a.customLabel, 'Other');
  assert.equal(a.options[0].label, 'A');
});

test('ask: an option previews §2 actions — including a video frame', () => {
  const a = askOf({ question: 'Which frame?', options: [
    { label: '0:04', actions: [{ op: 'frame', index: 120 }] },
    { label: 'Sepia', actions: [{ op: 'filter', mode: 'sepia' }] },
  ] });
  assert.deepEqual(a.options[0].actions, [{ op: 'frame', index: 120 }]);
  assert.equal(a.options[1].actions[0].op, 'filter');
});

test('ask: an option names an existing image — exactly one reference kind', () => {
  const a = askOf({ question: 'Which?', options: [
    { label: 'Web', image: { url: 'https://example.com/cat.jpg' } },
    { label: 'Project', image: { projectId: 'p_12' } },
  ] });
  assert.deepEqual(a.options[0].image, { url: 'https://example.com/cat.jpg' });
  assert.deepEqual(a.options[1].image, { projectId: 'p_12' });
  // scanIndex is the extension profile's reference (§8) and stays an integer.
  assert.deepEqual(askOf({ question: 'Q', options: [{ label: 'A', image: { scanIndex: 3 } }, { label: 'B' }] })
    .options[0].image, { scanIndex: 3 });
});

test('ask: a non-http image URL is refused — a plan never points the client at data:/file:', () => {
  for (const url of ['data:image/png;base64,AAA', 'file:///etc/passwd', 'javascript:alert(1)', 'ftp://h/x.png'])
    assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'A', image: { url } }, { label: 'B' }] }), /http\(s\) URL/);
});

test('ask: an option cannot be both a render and a reference', () => {
  assert.throws(() => askPlan({ question: 'Q', options: [
    { label: 'A', actions: [{ op: 'rotate', dir: 'left' }], image: { projectId: 'p1' } }, { label: 'B' },
  ] }), /both "actions" and "image"/);
});

test('ask: option counts outside 2..5 fail — a card nobody can answer is an error', () => {
  const opt = (i) => ({ label: `o${i}` });
  assert.throws(() => askPlan({ question: 'Q', options: [] }), /2\.\.5/);
  assert.throws(() => askPlan({ question: 'Q', options: [opt(1)] }), /2\.\.5/);
  assert.throws(() => askPlan({ question: 'Q', options: [1, 2, 3, 4, 5, 6].map(opt) }), /2\.\.5/);
  assert.equal(askOf({ question: 'Q', options: [1, 2, 3, 4, 5].map(opt) }).options.length, ASK_LIMITS.maxOptions);
});

test('ask: malformed cards are plan errors, not silent drops', () => {
  const two = [{ label: 'A' }, { label: 'B' }];
  assert.throws(() => askPlan({ options: two }), /"ask.question"/);
  assert.throws(() => askPlan({ question: '   ', options: two }), /"ask.question"/);
  assert.throws(() => askPlan({ question: 'x'.repeat(ASK_LIMITS.question + 1), options: two }), /longer than/);
  assert.throws(() => askPlan({ question: 'Q', options: two, mode: 'maybe' }), /"ask.mode"/);
  assert.throws(() => askPlan({ question: 'Q', options: two, allowCustom: 'yes' }), /"ask.allowCustom"/);
  assert.throws(() => askPlan({ question: 'Q', options: 'nope' }), /"ask.options" must be an array/);
  assert.throws(() => askPlan({ question: 'Q', options: two, sneaky: 1 }), /unknown field "sneaky"/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'A', sneaky: 1 }, { label: 'B' }] }), /unknown field "sneaky"/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: '' }, { label: 'B' }] }), /"label"/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'x'.repeat(ASK_LIMITS.label + 1) }, { label: 'B' }] }), /longer than/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'A', image: {} }, { label: 'B' }] }), /exactly one of/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'A', image: { url: 'https://a/', projectId: 'p' } }, { label: 'B' }] }), /exactly one of/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'A', image: { nope: 'x' } }, { label: 'B' }] }), /unknown field "nope"/);
  assert.throws(() => askPlan({ question: 'Q', options: [{ label: 'A', image: { scanIndex: -1 } }, { label: 'B' }] }), /integer >= 0/);
  assert.throws(() => askPlan({ question: 'Q', options: two, customLabel: '' }), /"ask.customLabel"/);
  assert.throws(() => askPlan({ ...{ question: 'Q', options: two } , mode: 1 }), /"ask.mode"/);
  assert.throws(() => parseOpPlan('{"version":1,"reply":"ok","ask":"hello"}'), /"ask" must be an object/);
});

test('ask: an editor-settings op in a preview drops the picture — a preview renders, never configures', () => {
  const p = askPlan({ question: 'Q', options: [
    { label: 'Dark', actions: [{ op: 'theme', mode: 'dark' }] }, { label: 'B' },
  ] });
  assert.ok(p.warnings.some((w) => /Dropped the preview for ask option 1 \("Dark"\).*not allowed inside variants/.test(w)),
    JSON.stringify(p.warnings));
  assert.strictEqual(p.ask.options[0].actions, undefined);
});

test('ask: an unknown op inside a preview is dropped with a warning, like anywhere else', () => {
  const p = askPlan({ question: 'Q', options: [
    { label: 'A', actions: [{ op: 'teleport' }, { op: 'rotate', dir: 'left' }] }, { label: 'B' },
  ] });
  assert.deepEqual(p.ask.options[0].actions.map((a) => a.op), ['rotate']);
  assert.ok(p.warnings.some((w) => /teleport/.test(w)));
});

test('ask: a plan may both edit and ask — the edits still parse', () => {
  const p = parseOpPlan(JSON.stringify({ version: 1, reply: 'cropped; now pick a tint',
    actions: [{ op: 'rotate', dir: 'right' }],
    ask: { question: 'Which tint?', options: [{ label: 'Sepia' }, { label: 'B&W' }] } }));
  assert.equal(p.actions.length, 1);
  assert.equal(p.ask.options.length, 2);
});

test('askAnswerText: the picked labels become the next user turn', () => {
  const a = askOf({ question: 'Q', mode: 'multi', options: [{ label: 'Sepia' }, { label: 'B&W' }] });
  assert.equal(askAnswerText(a, { picked: [a.options[0]] }), 'Sepia');
  assert.equal(askAnswerText(a, { picked: a.options }), 'Sepia, B&W');
  assert.equal(askAnswerText(a, { picked: ['Sepia', 'B&W'] }), 'Sepia, B&W');   // bare labels too
  assert.equal(askAnswerText(a, { picked: [] }), '');
});

test('askAnswerText: typed custom text wins, trimmed and capped', () => {
  const a = askOf({ question: 'Q', allowCustom: true, options: [{ label: 'Sepia' }, { label: 'B&W' }] });
  assert.equal(askAnswerText(a, { picked: [a.options[0]], custom: '  a warm green  ' }), 'a warm green');
  assert.equal(askAnswerText(a, { custom: 'x'.repeat(ASK_LIMITS.answer + 50) }).length, ASK_LIMITS.answer);
  assert.equal(askAnswerText(a, {}), '');
});

test('validateAsk: null in, null out (the field is optional)', () => {
  assert.equal(validateAsk(null, []), null);
  assert.equal(validateAsk(undefined, []), null);
});
