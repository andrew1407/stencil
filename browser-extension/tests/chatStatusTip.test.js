// Tests for src/lib/statusTip.js — the assistant "…" trigger's rich status
// tooltip, extracted from popup/assistant.js. The row/foot builders are pure; the
// factory is driven with a stub document like the other DOM-adjacent suites.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { gearStatusRows, gearTipFootText, createChatStatusTip } from '../src/lib/chat/statusTip.js';
import { stubDoc, stubEl, stubWin } from './helpers/domStub.js';

const LABELS = { none: 'None (assistant off)', ollama: 'Ollama', anthropic: 'Anthropic' };

// ── gearStatusRows ──

test('no probe yet: a single amber "checking" row', () => {
  assert.deepEqual(gearStatusRows(null, LABELS),
    [{ label: 'Status', value: 'Checking the configured LLM…', state: 'connecting' }]);
});

test('provider none: turned off, and nothing is sent anywhere', () => {
  const rows = gearStatusRows({ provider: 'none' }, LABELS);
  assert.equal(rows.length, 2);
  assert.deepEqual(rows[0], { label: 'Provider', value: LABELS.none });
  assert.equal(rows[1].state, 'error');
  assert.match(rows[1].value, /nothing is sent anywhere/);
});

test('a reachable provider: provider / endpoint (scheme stripped) / model / green status', () => {
  const rows = gearStatusRows(
    { provider: 'ollama', url: 'http://localhost:11434', model: 'llava', ok: true, detail: '2 models' },
    LABELS);
  assert.deepEqual(rows.map((r) => r.label), ['Provider', 'Endpoint', 'Model', 'Status']);
  assert.equal(rows[0].value, 'Ollama');
  assert.equal(rows[1].value, 'localhost:11434');
  assert.equal(rows[2].value, 'llava');
  assert.deepEqual(rows[3], { label: 'Status', value: 'Connected — 2 models', state: 'ok' });
});

test('an unlabelled provider falls back to its id; no model means the server default', () => {
  const rows = gearStatusRows({ provider: 'server', ok: false, detail: '' }, LABELS);
  assert.equal(rows[0].value, 'server');
  assert.equal(rows.find((r) => r.label === 'Model').value, 'server default');
  assert.deepEqual(rows.at(-1), { label: 'Status', value: 'Unreachable', state: 'error' });
});

// ── gearTipFootText ──

test('reachable (or off, or still probing): the table says it all, the foot only invites the click', () => {
  for (const probe of [null, { provider: 'none' }, { provider: 'anthropic', ok: true }]) {
    assert.equal(gearTipFootText(probe), 'Click to configure the assistant');
  }
});

test('unreachable: the foot names the endpoint, and Ollama gets its "start Ollama" nudge', () => {
  assert.equal(gearTipFootText({ provider: 'ollama', ok: false, url: 'http://localhost:11434' }),
    'No LLM reachable at http://localhost:11434 — start Ollama or configure another provider.\n'
    + 'Click to configure the assistant');
  assert.equal(gearTipFootText({ provider: 'anthropic', ok: false }),
    'No LLM reachable — configure another provider.\nClick to configure the assistant');
});

// ── createChatStatusTip (stub DOM) ──

const build = () => {
  const doc = stubDoc();
  const anchor = stubEl('button');
  const tip = createChatStatusTip({
    doc, getAnchor: () => anchor, labels: LABELS, win: stubWin(),
  });
  return { doc, anchor, tip };
};

test('the tip lands on the body, hidden, and show() renders the table + foot and marks it visible', () => {
  const { doc, tip } = build();
  assert.equal(doc.body.children[0], tip.el);
  assert.equal(tip.el.className, 'chat-status-tip');
  assert.ok(!tip.el.classList.contains('visible'));
  tip.show();
  assert.ok(tip.el.classList.contains('visible'));
  const [table, foot] = tip.el.children.slice(-2);
  assert.equal(table.tag, 'table');
  assert.equal(table.children.length, 1, 'no probe yet: the single "checking" row');
  assert.equal(foot.className, 'chat-status-tip-foot');
  assert.equal(foot.textContent, 'Click to configure the assistant');
});

test('setProbe re-renders a VISIBLE tip in place; hide() takes the visible class away', () => {
  const { tip } = build();
  tip.show();
  tip.setProbe({ provider: 'ollama', url: 'http://x', model: 'm', ok: true });
  const table = tip.el.children.at(-2);
  assert.equal(table.children.length, 4, 'provider / endpoint / model / status');
  tip.hide();
  assert.ok(!tip.el.classList.contains('visible'));
});

test('wire(): hover shows, leave hides, and the focus right after pointerdown is suppressed once', () => {
  const { anchor, tip } = build();
  tip.wire(anchor);
  anchor.fire('pointerenter');
  assert.ok(tip.el.classList.contains('visible'));
  anchor.fire('pointerleave');
  assert.ok(!tip.el.classList.contains('visible'));
  // The click gesture: pointerdown hides (and arms the suppression), the focus that
  // follows must NOT re-show the tip over the menu the click just opened…
  anchor.fire('pointerdown');
  anchor.fire('focus');
  assert.ok(!tip.el.classList.contains('visible'), 'the one post-click focus is suppressed');
  // …but a later, real focus (keyboard Tab) shows it again.
  anchor.fire('focus');
  assert.ok(tip.el.classList.contains('visible'));
  anchor.fire('blur');
  assert.ok(!tip.el.classList.contains('visible'));
});
