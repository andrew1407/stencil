// The §11 choice card (chatAskCard) built against a stub DOM, the app.chat facade's source pins
// and the assistant settings modal's commit-on-Save. Split from chat-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;
const once = (id) => assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);

// §11 choice card (chatAskCard) against a stub DOM, pure element construction: the SHAPE is what is
// pinned — widget per mode, previews, custom row, submit gate — and model text never becomes markup.
const stubDom = () => {
  const make = (tag) => {
    const el = {
      tagName: String(tag).toUpperCase(), children: [], className: '', dataset: {}, style: {},
      type: '', name: '', value: '', placeholder: '', checked: false, disabled: false,
      src: '', alt: '', _text: '', _html: '', _listeners: {},
      set textContent(v) { this._text = String(v); this.children.length = 0; },
      get textContent() { return this._text || this.children.map((c) => c.textContent).join(''); },
      set innerHTML(v) { this._html = String(v); },
      get innerHTML() { return this._html; },
      appendChild(c) { this.children.push(c); return c; },
      append(...cs) { this.children.push(...cs); },
      remove() { removed.push(this); },
      addEventListener(t, fn) { (this._listeners[t] ||= []).push(fn); },
      fire(t) { for (const fn of this._listeners[t] || []) fn(); },
      classList: { add(c) { el.className += ` ${c}`; } },
    };
    return el;
  };
  const removed = [];
  globalThis.document = { createElement: make };
  return { removed };
};

const walk = (el, out = []) => { out.push(el); for (const c of el.children) walk(c, out); return out; };
const byClass = (root, cls) => walk(root).filter((e) => String(e.className).split(/\s+/).includes(cls));

test('chatAskCard: single mode renders radios, a preview per rendered option, and a gated Submit', async () => {
  stubDom();
  const { chatAskCard } = await import('../js/ui/chatView.js');
  const ask = { question: 'Which tint?', mode: 'single', allowCustom: false, customLabel: 'Other',
    options: [{ label: 'Sepia' }, { label: 'B&W' }] };
  const sent = [];
  const card = chatAskCard(ask, { onSubmit: (a) => sent.push(a), previews: [{ index: 1, label: 'B&W', dataUrl: 'data:image/png;base64,AA' }] });

  const inputs = walk(card).filter((e) => e.tagName === 'INPUT');
  assert.deepEqual(inputs.map((i) => i.type), ['radio', 'radio']);
  assert.equal(new Set(inputs.map((i) => i.name)).size, 1, 'radios share one group name');
  // Only the option that HAS a preview gets a picture.
  assert.equal(byClass(card, 'chat-ask-thumb').length, 1);
  assert.equal(byClass(card, 'chat-ask-thumb')[0].src, 'data:image/png;base64,AA');
  // The question and labels are text nodes, never markup.
  assert.equal(byClass(card, 'chat-ask-q')[0].textContent, 'Which tint?');
  assert.deepEqual(byClass(card, 'chat-ask-label').map((l) => l.textContent), ['Sepia', 'B&W']);

  const submit = byClass(card, 'chat-ask-submit')[0];
  assert.equal(submit.disabled, true, 'nothing picked → Submit is disabled');
  submit.fire('click');
  assert.deepEqual(sent, [], 'a disabled Submit sends nothing');

  inputs[0].checked = true;
  inputs[0].fire('change');
  assert.equal(submit.disabled, false);
  submit.fire('click');
  assert.deepEqual(sent, ['Sepia']);
});

test('chatAskCard: multi mode renders checkboxes and joins every pick', async () => {
  stubDom();
  const { chatAskCard } = await import('../js/ui/chatView.js?multi');
  const ask = { question: 'Which images?', mode: 'multi', allowCustom: false, customLabel: 'Other',
    options: [{ label: 'Cat' }, { label: 'Rabbit' }, { label: 'Hare' }] };
  const sent = [];
  const card = chatAskCard(ask, { onSubmit: (a) => sent.push(a) });
  const inputs = walk(card).filter((e) => e.tagName === 'INPUT');
  assert.deepEqual(inputs.map((i) => i.type), ['checkbox', 'checkbox', 'checkbox']);
  inputs[0].checked = true; inputs[2].checked = true;
  inputs[0].fire('change');
  byClass(card, 'chat-ask-submit')[0].fire('click');
  assert.deepEqual(sent, ['Cat, Hare']);
});

test('chatAskCard: the custom row wins, and typing in it selects it', async () => {
  stubDom();
  const { chatAskCard } = await import('../js/ui/chatView.js?custom');
  const ask = { question: 'Which tint?', mode: 'single', allowCustom: true, customLabel: 'Something else…',
    options: [{ label: 'Sepia' }, { label: 'B&W' }] };
  const sent = [];
  const card = chatAskCard(ask, { onSubmit: (a) => sent.push(a) });
  const inputs = walk(card).filter((e) => e.tagName === 'INPUT');
  assert.equal(inputs.length, 4, 'two options + the custom radio + its text field');
  const customText = byClass(card, 'chat-ask-custom-text')[0];
  assert.equal(customText.placeholder, 'Something else…');
  customText.value = '  a warm green  ';
  customText.fire('input');                       // typing picks the custom row…
  assert.equal(inputs[2].checked, true);
  byClass(card, 'chat-ask-submit')[0].fire('click');
  assert.deepEqual(sent, ['a warm green'], 'trimmed, and it beats any picked label');
});

test('chatAskCard: answering locks the card — it can never fire twice', async () => {
  stubDom();
  const { chatAskCard } = await import('../js/ui/chatView.js?lock');
  const ask = { question: 'Q', mode: 'single', allowCustom: false, customLabel: 'Other',
    options: [{ label: 'A' }, { label: 'B' }] };
  const sent = [];
  const card = chatAskCard(ask, { onSubmit: (a) => sent.push(a) });
  const inputs = walk(card).filter((e) => e.tagName === 'INPUT');
  const submit = byClass(card, 'chat-ask-submit')[0];
  inputs[1].checked = true; inputs[1].fire('change');
  submit.fire('click');
  submit.fire('click');                            // a second click on the removed button
  assert.deepEqual(sent, ['B']);
  assert.ok(inputs.every((i) => i.disabled), 'every input is disabled once answered');
  assert.equal(byClass(card, 'chat-ask-sent')[0].textContent, 'B');
  assert.match(card.className, /chat-ask-answered/);
});

// app.chat's wiring is DOM-bound, so the source is asserted: history rides the SETTLED-transcript path
// (rowsToMessages), abort the Stop button's turnAbort. stencil.chat is driven for real elsewhere.
test('app.chat exposes history (rowsToMessages over the log), abort, and isSending', () => {
  const src = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  assert.ok(src.includes('history: () => rowsToMessages(chatLog()).map((m) => ({ role: m.role, text: m.text }))'));
  assert.match(src, /abort: \(\) => \{[\s\S]{0,120}turnAbort\?\.abort\(\);/);
  assert.ok(src.includes('get isSending() { return sending; }'));
  // clear rides the same shared path as the trash button, refused mid-turn.
  assert.match(src, /clear: \(\) => \{[\s\S]{0,200}clearSharedConversation\(app\);/);
});

// ── Assistant modal commits on Save (desktop dialog parity) ──
test('assistant settings modal has Cancel/Save and only Save writes storage', () => {
  for (const id of ['chat-settings-cancel', 'chat-settings-save']) once(id);
  const src = readFileSync(new URL('../js/ui/llmSettingsModal.js', import.meta.url), 'utf8');
  // Exactly one persist() call site — the Save handler; field edits only touch
  // the working copy, so every other close path discards.
  assert.strictEqual(src.split('persist();').length - 1, 1, 'a single persist() call site');
  assert.match(src, /chat-settings-save'\)\.addEventListener\('click'[\s\S]{0,400}persist\(\);/);
  assert.ok(src.includes("$('chat-settings-cancel').addEventListener('click', () => shell.close());"));
  // Reopening reloads from storage — that is what makes a close a discard. Matched on the
  // two statements rather than one line of source: onOpen also starts the height ease.
  assert.match(src, /onOpen: \(\) => \{[\s\S]{0,200}settings = loadLlmSettings\(\);\s*render\(\);/);
});
