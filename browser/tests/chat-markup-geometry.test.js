// The chat panel's pure geometry helpers (clamp/resize/dock/compact) and the gear tooltip's
// status table, plus the handles and dot in the composed markup. From chat-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import { layout } from '../js/ui/layout.js';
import { clampFloatRect, resizeFloatRect, dockZoneAt, compactChatRect, gearStatusRows, gearTipFootText, FLOAT_MIN_W, FLOAT_MIN_H, DOCK_ZONE_BAND, COMPACT_CHAT_W, COMPACT_CHAT_H } from '../js/ui/chat/panel.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;

// ── Pure geometry helpers backing the drag/dock UX ──
test('clampFloatRect keeps the WHOLE panel inside the viewport (right/bottom edges too)', () => {
  // Panel pushed past the right edge → x pulled back so x + w == vw.
  assert.deepStrictEqual(clampFloatRect({ x: 1200, y: 50, w: 360, h: 440 }, 1280, 800),
    { x: 920, y: 50, w: 360, h: 440 });
  // Past the bottom edge → y pulled back so y + h == vh.
  assert.deepStrictEqual(clampFloatRect({ x: 10, y: 700, w: 360, h: 440 }, 1280, 800),
    { x: 10, y: 360, w: 360, h: 440 });
  // Negative origin clamps to 0; size clamps to the viewport and the minimums.
  const r = clampFloatRect({ x: -50, y: -20, w: 5000, h: 5000 }, 1280, 800);
  assert.deepStrictEqual(r, { x: 0, y: 0, w: 1280, h: 800 });
  const tiny = clampFloatRect({ x: 0, y: 0, w: 10, h: 10 }, 1280, 800);
  assert.strictEqual(tiny.w, FLOAT_MIN_W);
  assert.strictEqual(tiny.h, FLOAT_MIN_H);
  // Bad/missing input falls back to sane defaults, still fully inside.
  const d = clampFloatRect(null, 1280, 800);
  assert.ok(d.x >= 0 && d.y >= 0 && d.x + d.w <= 1280 && d.y + d.h <= 800);
});

test('resizeFloatRect: each edge/corner resizes with the opposite edge anchored', () => {
  const vw = 1280, vh = 800;
  const r = { x: 400, y: 300, w: 360, h: 300 };
  // East/south move only the size; west/north move origin AND size (anchor fixed).
  assert.deepStrictEqual(resizeFloatRect(r, 'e', 50, 999, vw, vh), { x: 400, y: 300, w: 410, h: 300 });
  assert.deepStrictEqual(resizeFloatRect(r, 's', 999, 40, vw, vh), { x: 400, y: 300, w: 360, h: 340 });
  assert.deepStrictEqual(resizeFloatRect(r, 'w', -60, 0, vw, vh), { x: 340, y: 300, w: 420, h: 300 });
  assert.deepStrictEqual(resizeFloatRect(r, 'n', 0, -50, vw, vh), { x: 400, y: 250, w: 360, h: 350 });
  // Corners resize both axes.
  assert.deepStrictEqual(resizeFloatRect(r, 'ne', 30, -20, vw, vh), { x: 400, y: 280, w: 390, h: 320 });
  assert.deepStrictEqual(resizeFloatRect(r, 'sw', -30, 20, vw, vh), { x: 370, y: 300, w: 390, h: 320 });
  // Shrinking stops at the minimum size; the anchored edge never moves.
  const min = resizeFloatRect(r, 'w', 999, 0, vw, vh);
  assert.deepStrictEqual(min, { x: 400 + 360 - FLOAT_MIN_W, y: 300, w: FLOAT_MIN_W, h: 300 });
  const minN = resizeFloatRect(r, 'n', 0, 999, vw, vh);
  assert.deepStrictEqual(minN, { x: 400, y: 300 + 300 - FLOAT_MIN_H, w: FLOAT_MIN_H === 220 ? 360 : 360, h: FLOAT_MIN_H });
  // Growing stops at the viewport: the moving edge is clamped, the anchor stays.
  assert.deepStrictEqual(resizeFloatRect(r, 'e', 9999, 0, vw, vh), { x: 400, y: 300, w: vw - 400, h: 300 });
  assert.deepStrictEqual(resizeFloatRect(r, 'w', -9999, 0, vw, vh), { x: 0, y: 300, w: 760, h: 300 });
  assert.deepStrictEqual(resizeFloatRect(r, 's', 0, 9999, vw, vh), { x: 400, y: 300, w: 360, h: vh - 300 });
  assert.deepStrictEqual(resizeFloatRect(r, 'n', 0, -9999, vw, vh), { x: 400, y: 0, w: 360, h: 600 });
});

test('float edge/corner resize handles are composed in (SE stays #chat-resizer)', () => {
  for (const dir of ['n', 's', 'e', 'w', 'ne', 'nw', 'sw']) {
    assert.strictEqual(count(`chat-float-handle-${dir}"`), 1, `handle ${dir} present once`);
    assert.ok(markup.includes(`data-dir="${dir}"`), `handle ${dir} carries its direction`);
  }
  assert.strictEqual(count('chat-float-handle-se'), 0, 'no se handle — #chat-resizer owns that corner');
});

test('dockZoneAt: edge bands dock, the middle keeps floating, corners pick the nearest edge', () => {
  const vw = 1280, vh = 800;
  assert.strictEqual(dockZoneAt(10, 400, vw, vh), 'left');
  assert.strictEqual(dockZoneAt(vw - 10, 400, vw, vh), 'right');
  assert.strictEqual(dockZoneAt(640, 10, vw, vh), 'top');
  assert.strictEqual(dockZoneAt(640, vh - 10, vw, vh), 'bottom');
  assert.strictEqual(dockZoneAt(640, 400, vw, vh), null);            // centre → keep floating
  assert.strictEqual(dockZoneAt(DOCK_ZONE_BAND + 1, 400, vw, vh), null);   // just outside the band
  // Corner: the nearest edge wins.
  assert.strictEqual(dockZoneAt(10, 40, vw, vh), 'left');
  assert.strictEqual(dockZoneAt(40, 10, vw, vh), 'top');
});

test('compactChatRect: the popover gestures float the panel small, pinned to the icon', () => {
  const vw = 1280, vh = 800;
  // A toolbar icon: the compact panel opens below it, left edges aligned.
  assert.deepStrictEqual(compactChatRect({ left: 500, top: 10, bottom: 40 }, vw, vh),
    { x: 500, y: 48, w: COMPACT_CHAT_W, h: COMPACT_CHAT_H });
  // An icon near the right edge: pulled left so the panel stays on-screen.
  const right = compactChatRect({ left: 1200, top: 10, bottom: 40 }, vw, vh);
  assert.ok(right.x + right.w <= vw, 'never past the right edge');
  // An icon near the bottom: flipped ABOVE the anchor instead of overflowing.
  const above = compactChatRect({ left: 100, top: 700, bottom: 730 }, vw, vh);
  assert.strictEqual(above.y, 700 - 8 - COMPACT_CHAT_H);
  // A viewport smaller than the compact size: shrunk and still fully inside.
  const small = compactChatRect({ left: 20, top: 5, bottom: 30 }, 300, 400);
  assert.ok(small.w <= 300 && small.h <= 400);
  assert.ok(small.x >= 0 && small.y >= 0 && small.x + small.w <= 300 && small.y + small.h <= 400);
});

// ── The gear's status tooltip is a table: rows carry ALL provider info, the
// Status cell is coloured via `state`, and the footer keeps the hints ──
test('gearStatusRows: reachable → provider/endpoint/model rows + green status', () => {
  const rows = gearStatusRows({ ok: true, provider: 'ollama', url: 'http://localhost:11434', model: 'llama3.2-vision', detail: 'v0.5' });
  assert.deepStrictEqual(rows.map((r) => r.label), ['Provider', 'Endpoint', 'Model', 'Status']);
  assert.deepStrictEqual(rows[0], { label: 'Provider', value: 'Ollama' });
  assert.deepStrictEqual(rows[1], { label: 'Endpoint', value: 'localhost:11434' });
  assert.deepStrictEqual(rows[2], { label: 'Model', value: 'llama3.2-vision' });
  assert.deepStrictEqual(rows[3], { label: 'Status', value: 'Connected — v0.5', state: 'ok' });
  // Connected says nothing beyond the table — no worked examples, just the call to action.
  const foot = gearTipFootText({ ok: true, provider: 'ollama', url: 'http://localhost:11434' }).split('\n');
  assert.deepStrictEqual(foot, ['Click to configure the assistant']);
});

test('gearStatusRows: unreachable → red status; footer keeps the call to action', () => {
  const rows = gearStatusRows({ ok: false, provider: 'ollama', url: 'http://localhost:11434', model: '', detail: 'fetch failed' });
  assert.deepStrictEqual(rows[2], { label: 'Model', value: 'server default' });
  assert.deepStrictEqual(rows[3], { label: 'Status', value: 'fetch failed', state: 'error' });
  const foot = gearTipFootText({ ok: false, provider: 'ollama', url: 'http://localhost:11434' }).split('\n');
  assert.strictEqual(foot[0], 'No LLM reachable at http://localhost:11434 — start Ollama or configure another provider.');
  assert.strictEqual(foot[1], 'Click to configure the assistant');
  // Non-ollama providers skip the "start Ollama" hint.
  assert.ok(gearTipFootText({ ok: false, provider: 'stencil-server', url: 'http://srv:8090' })
    .includes('No LLM reachable at http://srv:8090 — configure another provider.'));
});

test('gearStatusRows: provider none → off rows, no endpoint noise, plain footer', () => {
  const rows = gearStatusRows({ ok: false, provider: 'none', url: '', detail: 'assistant turned off' });
  assert.deepStrictEqual(rows, [
    { label: 'Provider', value: 'None (turned off)' },
    { label: 'Status', value: 'Assistant turned off — nothing is sent anywhere', state: 'error' },
  ]);
  assert.strictEqual(gearTipFootText({ ok: false, provider: 'none' }), 'Click to configure the assistant');
});

test('gearStatusRows: probe in flight → a single amber "checking" row, no CTA noise', () => {
  assert.deepStrictEqual(gearStatusRows(null),
    [{ label: 'Status', value: 'Checking the configured LLM…', state: 'connecting' }]);
  assert.strictEqual(gearTipFootText(null), 'Click to configure the assistant');
});

test('status dot ships in the connecting state on the gear', () => {
  assert.ok(markup.includes('id="chat-status-dot" class="conn-status conn-status-connecting"'), 'dot present, amber until probed');
});
