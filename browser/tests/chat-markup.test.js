import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// Layout transitively registers every ui component (chat panel + LLM settings modal
// included) and must compose their markup exactly once, appended after the original
// regions (the REGIONS order is load-bearing).
import { layout } from '../js/ui/layout.js';
import { clampFloatRect, resizeFloatRect, dockZoneAt, compactChatRect, gearStatusRows, gearTipFootText, FLOAT_MIN_W, FLOAT_MIN_H, DOCK_ZONE_BAND, COMPACT_CHAT_W, COMPACT_CHAT_H } from '../js/ui/chatPanel.js';
import { COMPONENTS_CSS } from './helpers/css.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;
const once = (id) => assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);

test('chat panel ids are present exactly once', () => {
  for (const id of [
    'chat-panel', 'chat-header', 'chat-title', 'chat-status-dot',
    'chat-dock-left-btn', 'chat-dock-top-btn', 'chat-dock-bottom-btn', 'chat-dock-right-btn', 'chat-float-btn',
    'chat-settings-btn', 'chat-close', 'chat-transcript', 'chat-empty', 'chat-attachments',
    'chat-attach-btn', 'chat-attach-input', 'chat-input', 'chat-send', 'chat-voice', 'chat-resizer',
    'chat-input-sizer',
  ]) once(id);
});

test('LLM settings modal ids are present exactly once', () => {
  for (const id of [
    'chat-settings-overlay', 'chat-settings-close', 'chat-provider',
    'chat-base-url-row', 'chat-base-url', 'chat-model', 'chat-api-key-row', 'chat-api-key',
    'chat-server-row', 'chat-server-select', 'chat-server-status-row', 'chat-server-status',
    'chat-cors-note', 'chat-save-chats',
  ]) once(id);
});

test('chat persistence opt-in (§12) is a checkbox that ships unchecked', () => {
  const i = markup.indexOf('id="chat-save-chats"');
  const tag = markup.slice(markup.lastIndexOf('<input', i), markup.indexOf('>', i) + 1);
  assert.ok(tag.includes('type="checkbox"'), 'saveChats is a checkbox');
  assert.ok(!tag.includes('checked'), 'ships OFF — persistence is an explicit opt-in');

  // §12.2: who can read a saved chat has to be VISIBLE at the toggle, not just in
  // a title= the user never hovers. A server project's chat carries the project's
  // access, and "save chats with projects" doesn't sound like "my colleagues can
  // read what I asked for".
  const note = markup.slice(markup.indexOf('id="chat-save-chats-note"'));
  const body = note.slice(0, note.indexOf('</div>'));
  assert.match(body, /shared with/, 'the sharing consequence is stated next to the toggle');
});

test('toolbar toggle + glyphs are composed in', () => {
  once('chat-btn');
  assert.ok(markup.includes('ic-sparkle'), 'sparkle glyph used');
  assert.ok(markup.includes('ic-send'), 'send glyph used');
});

test('provider select offers the three contract providers plus the local-only off switch', () => {
  const sel = markup.slice(markup.indexOf('id="chat-provider"'));
  const options = sel.slice(0, sel.indexOf('</select>'));
  for (const v of ['none', 'ollama', 'openai-compat', 'stencil-server']) {
    assert.ok(options.includes(`<option value="${v}"`), `provider option ${v} present`);
  }
});

test('chat regions append at the END of the body (after the install button)', () => {
  assert.ok(markup.indexOf('id="install-host"') < markup.indexOf('id="chat-panel"'), 'panel after install');
  assert.ok(markup.indexOf('id="chat-panel"') < markup.indexOf('id="chat-settings-overlay"'), 'modal after panel');
});

test('CORS help note for local providers is present', () => {
  assert.ok(markup.includes('OLLAMA_ORIGINS'), 'Ollama origins hint');
  assert.ok(markup.includes('enable CORS'), 'LM Studio CORS hint');
});

// ── Header ghosts + accent action row (send first, then media, then settings) ──
test('header buttons are chat-hbtn ghosts; composer is send + a … menu', () => {
  assert.strictEqual(count('class="chat-hbtn'), 6, '5 dock + close are header ghosts');
  // Only SEND and the … trigger stay inline; the rest live in the menu.
  assert.strictEqual(count('chat-abtn'), 2, 'send + the … trigger are the inline action buttons');
  const send = markup.indexOf('id="chat-send"');
  const more = markup.indexOf('id="chat-more-btn"');
  const menu = markup.indexOf('id="chat-more-menu"');
  assert.ok(send < more && more < menu, 'order: send, then the … trigger and its menu');
  for (const id of ['chat-attach-btn', 'chat-clear', 'chat-swap-sides', 'chat-settings-btn']) {
    assert.ok(markup.indexOf(`id="${id}"`) > menu, `${id} sits inside the … menu`);
  }
  // …in that order: swap sides sits between Clear history and Settings.
  assert.ok(markup.indexOf('id="chat-clear"') < markup.indexOf('id="chat-swap-sides"')
    && markup.indexOf('id="chat-swap-sides"') < markup.indexOf('id="chat-settings-btn"'));
  assert.strictEqual(count('chat-more-item'), 5, 'voice input + add image + clear history + swap sides + settings');
  assert.ok(markup.indexOf('id="chat-voice"') > menu && markup.indexOf('id="chat-voice"') < markup.indexOf('id="chat-attach-btn"'),
    'voice input is the first … item');
  assert.strictEqual(count('chat-title-text'), 1, 'truncatable title span present');
});

// ── One gear only, in the input row; actions grouped RIGHT of the input ──
test('input row: textarea first, then send · … (menu holds attach/clear/settings)', () => {
  const iInput = markup.indexOf('id="chat-input"');
  const iAttach = markup.indexOf('id="chat-attach-btn"');
  const iGear = markup.indexOf('id="chat-settings-btn"');
  const iSend = markup.indexOf('id="chat-send"');
  assert.ok(iInput < iSend && iSend < iAttach && iAttach < iGear, 'input stretches; send leads the action group');
  assert.strictEqual(count('chat-input-actions'), 1, 'right-aligned action group present');
  // The ONLY gear lives in the input row (inside the actions group, after the input).
  const header = markup.slice(markup.indexOf('id="chat-header"'), markup.indexOf('id="chat-transcript"'));
  assert.ok(!header.includes('chat-settings-btn'), 'no duplicate gear in the header');
  // The provider-status dot rides the … trigger — the menu's visible face now
  // that the gear itself lives inside the menu.
  const iMore = markup.indexOf('id="chat-more-btn"');
  const moreBtn = markup.slice(iMore, markup.indexOf('</button>', iMore));
  assert.ok(moreBtn.includes('id="chat-status-dot"'), 'status dot rides the … trigger');
});

test('send ships disabled (no text yet); empty state is a single subtle line', () => {
  assert.ok(markup.includes('id="chat-send" disabled'), 'send starts disabled');
  // Empty state = clickable prompt suggestions (data-prompt prefills the input).
  assert.ok(count('chat-suggest"') >= 3, 'at least three suggestion chips');
  assert.ok(markup.includes('data-prompt="Make it sepia"'), 'suggestion carries its prompt');
  assert.ok(markup.includes('data-prompt="3 variants: rotated · tinted · cropped"'), 'variants suggestion present');
  assert.strictEqual(count('chat-intro'), 0, 'the heavy intro card is gone');
});

test('edge drop zones are transient drag-time DOM — never in the static markup', () => {
  assert.strictEqual(count('chat-dock-zone'), 0);
});

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

// ── Install button vs chat panel: the CSS guard must exist (e2e found the floating
// "Get Stencil" button intercepting clicks on the docked panel's send button). ──
test('components.css guards the install button against the open chat panel', () => {
  const css = COMPONENTS_CSS;
  assert.ok(/body:has\(stencil-chat-panel\.chat-open\) #install-host \{ z-index: 89999; \}/.test(css),
    'install button drops beneath the open panel');
  assert.ok(css.includes('body:has(stencil-chat-panel.chat-open.chat-dock-right) #install-host'),
    'right dock shifts the install button left by --chat-size');
  assert.ok(css.includes('body:has(stencil-chat-panel.chat-open.chat-dock-bottom) #install-host'),
    'bottom dock lifts the install button by --chat-size');
  // The panel itself stacks below the modals but above the (lowered) install button.
  assert.ok(css.includes('z-index: 90000'), 'panel z-index present');
});

// ── #chat-btn keeps the Settings-row ghost COLOURS (a one-off rule once left it
// accent-on-accent, an invisible glyph), but its BOX belongs to the CONNECTIONS & LINKS
// row it sits in — the Settings padding made it 38x26 beside its 40x32 siblings. Hence:
// no padding/font-size override, and an inset-shadow outline so the box never jumps. ──
test('components.css styles #chat-btn: ghost colours, CONNECTIONS-row geometry', () => {
  const css = COMPONENTS_CSS;
  assert.match(css, /#settings-btn, #visuals-btn, #info-btn, #fullscreen-toggle, #incognito-toggle \{/,
    'the Settings-row ghost group still exists (without #chat-btn, which is not in that row)');
  assert.match(css, /#fullscreen-toggle\.active, #incognito-toggle\.active, #chat-btn\.active \{/,
    'active accent-fill group includes #chat-btn');
  assert.ok(!css.includes('#chat-btn.chat-btn-active'), 'no one-off active rule left behind');

  const start = css.indexOf('\n#chat-btn {');
  assert.ok(start > -1, '#chat-btn has its own idle rule');
  const rule = css.slice(start, css.indexOf('}', start));
  assert.ok(/background:\s*transparent/.test(rule), 'idle is transparent, not an accent square');
  assert.ok(/box-shadow:\s*inset 0 0 0 1px/.test(rule), 'idle outline is an inset shadow (keeps the 40x32 box)');
  assert.ok(!/\bpadding:/.test(rule), 'no padding override — it inherits the toolbar button box its siblings use');
  assert.ok(!/\bfont-size:/.test(rule), 'no font-size override — the glyph matches #connect-btn/#links-btn');
  assert.match(css, /#chat-btn\.active \{ box-shadow: none; \}/, 'the accent fill replaces the idle outline');
});

// ── Disabled + resizable-input + mobile CSS (assertable without a DOM) ──
test('components.css: disabled ghosts, resizable input, phone modal, touch targets', () => {
  const css = COMPONENTS_CSS;
  assert.ok(css.includes('.chat-hbtn:disabled'), 'disabled styling for the compact buttons');
  const inputRule = css.slice(css.indexOf('#chat-input {'), css.indexOf('}', css.indexOf('#chat-input {')));
  assert.ok(inputRule.includes('resize: none'), 'native corner grip is off — the sizer strip owns resizing');
  assert.ok(inputRule.includes('max-height'), 'input growth is clamped');
  assert.ok(css.includes('.chat-input-sizer::before'), 'slider-style input handle has the pill affordance');
  assert.ok(css.includes('#chat-input:focus'), 'prompt textarea has the app focus glow');
  // Phone breakpoint follows the app convention (animations.css: 680px = phones).
  const phone = css.slice(css.indexOf('@media (max-width: 680px)'));
  assert.ok(phone.includes('stencil-chat-panel.chat-open'), 'panel has a phone layout');
  // Phones get an ORDINARY MODAL: a CENTRED card over the dimmed #chat-backdrop,
  // with every placement/resize affordance (incl. the input sizer) hidden.
  assert.ok(/inset: 0 !important/.test(phone) && /margin: auto !important/.test(phone), 'card is centred, not edge-pinned');
  assert.ok(/width: min\(420px, calc\(100vw - 24px\)\) !important/.test(phone), 'card has a bounded, centred width');
  assert.ok(/height: min\(560px, calc\(100dvh - 24px\)\) !important/.test(phone), 'card height follows the dynamic viewport');
  assert.ok(/body:has\(stencil-chat-panel\.chat-open\) #chat-backdrop \{ display: block; \}/.test(phone), 'backdrop shows only under the phone modal');
  assert.ok(phone.includes('stencil-chat-panel .chat-input-sizer { display: none; }') || /chat-input-sizer \{ display: none/.test(phone), 'input sizer hidden in the modal');
  assert.ok(phone.includes('safe-area-inset-bottom'), 'input row respects the safe area');
  // The backdrop is inert everywhere else — the dock/float shapes leave the page usable.
  const backdrop = css.slice(css.indexOf('#chat-backdrop {'), css.indexOf('}', css.indexOf('#chat-backdrop {')));
  assert.ok(backdrop.includes('display: none'), 'backdrop is hidden (and click-through) by default');
  assert.ok(backdrop.includes('position: fixed') && backdrop.includes('inset: 0'), 'backdrop covers the viewport');
  // A dock band on a phone-width viewport overflows the document and makes the
  // browser widen the layout viewport — so the reflow padding is min-width gated.
  const reflow = css.slice(css.indexOf('body:not(.fullscreen-mode):has(stencil-chat-panel.chat-open:not(.chat-closing).chat-dock-left)') - 400);
  assert.ok(/@media \(min-width: 681px\) \{[\s\S]{0,400}?padding-left: calc\(var\(--chat-size/.test(reflow),
    'dock reflow padding never applies at phone widths');
  // Touch: ≥40px targets + hidden resize handles (installButton.js media convention).
  const touch = css.slice(css.indexOf('@media (hover: none) and (pointer: coarse)'));
  assert.ok(/\.chat-hbtn, \.chat-abtn \{ width: 40px; height: 40px; \}/.test(touch), '40px coarse-pointer targets');
  assert.ok(touch.includes('.chat-resizer { display: none; }'), 'resize handles hide on touch');
});

// ── §11 choice card (chatAskCard) ───────────────────────────────────────────
// Built against a minimal stub DOM: the card is pure element construction, so the
// contract worth pinning is its SHAPE (widget per mode, previews, custom row, the submit
// gate) and that model text never becomes markup.
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

// ── Scripting surface: app.chat exposes history / abort / isSending ──
// The wiring is DOM-bound, so assert the source registers the members and that
// history rides the SETTLED-transcript path (rowsToMessages over the shared log)
// while abort rides the Stop button's turnAbort. window.stencil.chat's side of the
// same contract is driven for real in consoleChatFacade.test.js.
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
  // Reopening reloads from storage — that is what makes a close a discard.
  assert.ok(src.includes('onOpen: () => { settings = loadLlmSettings(); voice = loadVoiceSettings(); render(); }'));
  // The voice settings ride the same commit: written only from the Save handler.
  assert.strictEqual(src.split('saveVoiceSettings(').length - 1, 1, 'a single saveVoiceSettings() call site');
  assert.match(src, /chat-settings-save'\)\.addEventListener\('click'[\s\S]{0,600}saveVoiceSettings\(/);
});
