// The §11 `ask` card in the extension profile (src/llm/opPlan.js): an option may NAME an
// existing image, but a preview the extension would have to RENDER or FETCH is dropped.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  LLM_SYSTEM_PROMPT, askAnswerText, ASK_LIMITS, DEFAULT_CUSTOM_LABEL,
} from '../src/llm/op/opPlan.js';
import { parse } from './helpers/opPlanHarness.js';

// §11, extension profile: the extension is not an editor, so an option may NAME an existing image
// (a listing index or an http(s) URL) but a preview it would have to RENDER is dropped with a warning.
const askOf = (ask, listingLength = 10) => parse({ version: 1, reply: 'ok', ask }, listingLength).ask;

test('ask: a single-choice card parses with its defaults', () => {
  const a = askOf({ question: 'Which image?', options: [{ label: 'The tabby' }, { label: 'The other' }] });
  assert.equal(a.question, 'Which image?');
  assert.equal(a.mode, 'single');
  assert.equal(a.allowCustom, false);
  assert.equal(a.customLabel, DEFAULT_CUSTOM_LABEL);
  assert.deepEqual(a.options.map((o) => o.label), ['The tabby', 'The other']);
});

test('ask: absent on an ordinary plan and on a chat-only reply', () => {
  assert.equal(parse({ version: 1, reply: 'hi', actions: [] }).ask, null);
  assert.equal(parse('just chatting').ask, null);
});

test('ask: an option names a scanned image by listing index', () => {
  const a = askOf({ question: 'Which?', mode: 'multi', options: [
    { label: 'From the page', image: { scanIndex: 3 } },
    { label: 'Also from the page', image: { scanIndex: 0 } },
  ] });
  assert.equal(a.mode, 'multi');
  assert.deepEqual(a.options[0].image, { scanIndex: 3 });
  assert.deepEqual(a.options[1].image, { scanIndex: 0 });
});

test('ask: an index past the listing keeps the option but loses its picture (+ warning)', () => {
  const p = parse({ version: 1, reply: 'ok', ask: { question: 'Which?', options: [
    { label: 'Way out there', image: { scanIndex: 99 } }, { label: 'Fine' },
  ] } }, 10);
  assert.equal(p.ask.options.length, 2);
  assert.equal(p.ask.options[0].image, undefined);
  assert.ok(p.warnings.some((w) => /image 99 is not in the list/.test(w)));
});

test('ask: a preview the extension cannot render is dropped with a warning, option intact', () => {
  const p = parse({ version: 1, reply: 'ok', ask: { question: 'Which tint?', options: [
    { label: 'Sepia', actions: [{ op: 'filter', mode: 'sepia' }] }, { label: 'B&W' },
  ] } });
  assert.deepEqual(p.ask.options.map((o) => o.label), ['Sepia', 'B&W']);
  assert.equal(p.ask.options[0].image, undefined);
  assert.ok(p.warnings.some((w) => /can't render a preview/.test(w)));
});

// §1's exception covers ask previews too: whatever ops an option's preview carries —
// including top-level-only and settings ops — the option survives, the plan never fails.
test('ask: a preview carrying top-level-only / settings ops drops the picture, not the plan', () => {
  for (const actions of [
    [{ op: 'save', name: 'x' }], [{ op: 'undo' }], [{ op: 'theme', mode: 'dark' }],
    [{ op: 'image', index: 2 }], [{ op: 'crop', spec: { bogus: 1 } }],
  ]) {
    const p = parse({ version: 1, reply: 'ok', ask: { question: 'Which?', options: [
      { label: 'A', actions }, { label: 'B' }] } });
    assert.equal(p.ask.options.length, 2);
    assert.equal(p.ask.options[0].image, undefined);
    assert.ok(p.warnings.some((w) => /can't render a preview/.test(w)));
  }
});

// Never fetched from the popup, which runs with <all_urls> and the user's cookies. The
// option survives without its picture, whatever the URL looks like.
test('ask: an image URL from the model is never fetched — option kept, picture dropped', () => {
  // Non-http(s) schemes are plan errors on every surface (registry ask schema, §11.1).
  for (const url of ['data:image/png;base64,AA', 'file:///etc/passwd', 'javascript:alert(1)']) {
    assert.throws(() => parse({ version: 1, reply: 'ok', ask: { question: 'Q', options: [
      { label: 'A', image: { url } }, { label: 'B' }] } }), /http\(s\) URL/, url);
  }
  for (const url of ['https://example.com/cat.jpg', 'http://169.254.169.254/latest/meta-data/']) {
    const p = parse({ version: 1, reply: 'ok', ask: { question: 'Q', options: [
      { label: 'A', image: { url } }, { label: 'B' }] } });
    assert.equal(p.ask.options.length, 2, url);
    assert.equal(p.ask.options[0].label, 'A', url);
    assert.equal(p.ask.options[0].image, undefined, url);
    assert.ok(p.warnings.some((w) => /not fetched here/.test(w)), url);
  }
});

test('ask: malformed cards are plan errors, not silent drops', () => {
  const two = [{ label: 'A' }, { label: 'B' }];
  const bad = (ask) => assert.throws(() => parse({ version: 1, reply: 'ok', ask }));
  bad({ options: two });                                   // no question
  bad({ question: '   ', options: two });
  bad({ question: 'x'.repeat(ASK_LIMITS.question + 1), options: two });
  bad({ question: 'Q', options: two, mode: 'maybe' });
  bad({ question: 'Q', options: 'nope' });
  bad({ question: 'Q', options: [] });                     // 0 options
  bad({ question: 'Q', options: [{ label: 'only' }] });     // 1 option
  bad({ question: 'Q', options: [1,2,3,4,5,6].map((i) => ({ label: `o${i}` })) });   // 6
  bad({ question: 'Q', options: two, sneaky: 1 });
  bad({ question: 'Q', options: [{ label: 'A', sneaky: 1 }, { label: 'B' }] });
  bad({ question: 'Q', options: [{ label: 'A', image: {} }, { label: 'B' }] });
  bad({ question: 'Q', options: [{ label: 'A', image: { url: 'https://a/', scanIndex: 1 } }, { label: 'B' }] });
  bad({ question: 'Q', options: [{ label: 'A', actions: [], image: { scanIndex: 1 } }, { label: 'B' }] });
  bad({ question: 'Q', options: two, allowCustom: 'yes' });
  assert.throws(() => parse('{"version":1,"reply":"ok","ask":"hello"}'), /"ask" must be an object/);
});

test('ask: 5 options is the cap and it parses', () => {
  const a = askOf({ question: 'Q', options: [1,2,3,4,5].map((i) => ({ label: `o${i}` })) });
  assert.equal(a.options.length, ASK_LIMITS.maxOptions);
});

test('askAnswerText: picked labels, or the typed custom text, become the next turn', () => {
  const a = askOf({ question: 'Q', mode: 'multi', allowCustom: true,
    options: [{ label: 'Cat' }, { label: 'Rabbit' }] });
  assert.equal(askAnswerText(a, { picked: [a.options[0]] }), 'Cat');
  assert.equal(askAnswerText(a, { picked: a.options }), 'Cat, Rabbit');
  assert.equal(askAnswerText(a, { picked: [a.options[0]], custom: '  a hare  ' }), 'a hare');
  assert.equal(askAnswerText(a, { custom: 'x'.repeat(ASK_LIMITS.answer + 20) }).length, ASK_LIMITS.answer);
  assert.equal(askAnswerText(a, {}), '');
});

test('the extension system prompt teaches `ask` (so the model can use it here too)', () => {
  assert.ok(LLM_SYSTEM_PROMPT.includes('"ask"'));
  assert.ok(LLM_SYSTEM_PROMPT.includes('scanIndex'));
});
