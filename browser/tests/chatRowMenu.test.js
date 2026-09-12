import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';

// ── The transcript row menu (chatRowMenu.js wireChatRowMenu) ────────────────
// Right-click on a settled chat message opens a small floating menu — Copy / Insert into
// prompt, plus Resend on USER rows — shared by the panel and the context-menu flyout
// (each passes its own composer hooks), driven here against the DOM-lite stub.

// A minimal live DOM: parent/child tracking (contains/remove), listeners with
// fire(), className/style/dataset — enough for the menu's build + delegation.
const makeEl = (tag = 'div') => {
  const el = {
    tagName: String(tag).toUpperCase(), children: [], parentNode: null,
    className: '', dataset: {}, style: {}, value: '', offsetWidth: 120, offsetHeight: 100,
    _text: '', _html: '', _listeners: {},
    set textContent(v) { this._text = String(v); this.children.length = 0; },
    get textContent() { return this._text || this.children.map((c) => c.textContent).join(''); },
    set innerHTML(v) { this._html = String(v); },
    get innerHTML() { return this._html; },
    setAttribute() {},
    select() {},
    appendChild(c) { c.parentNode = el; el.children.push(c); return c; },
    append(...cs) { for (const c of cs) el.appendChild(c); },
    remove() {
      const kids = el.parentNode?.children;
      const i = kids ? kids.indexOf(el) : -1;
      if (i >= 0) kids.splice(i, 1);
      el.parentNode = null;
    },
    contains(node) { return node === el || el.children.some((c) => c.contains?.(node)); },
    closest(sel) {
      for (let n = el; n; n = n.parentNode) {
        if (String(n.className).split(/\s+/).includes(sel.slice(1))) return n;
      }
      return null;
    },
    querySelector: () => null,
    addEventListener(t, fn) { (el._listeners[t] ||= []).push(fn); },
    removeEventListener() {},
    fire(t, ev = {}) { for (const fn of el._listeners[t] || []) fn(ev); },
  };
  return el;
};

const docListeners = [];
const stubDom = () => {
  docListeners.length = 0;
  const body = makeEl('body');
  globalThis.document = {
    createElement: makeEl,
    body,
    getElementById: () => null,   // notify() no-ops against this
    addEventListener: (t, fn, cap) => docListeners.push({ t, fn, cap }),
    removeEventListener: (t, fn, cap) => {
      const i = docListeners.findIndex((l) => l.t === t && l.fn === fn && !!l.cap === !!cap);
      if (i >= 0) docListeners.splice(i, 1);
    },
  };
  globalThis.window = {
    innerWidth: 1024, innerHeight: 768,
    addEventListener: () => {}, removeEventListener: () => {},
  };
  return { body };
};

const walk = (el, out = []) => { out.push(el); for (const c of el.children) walk(c, out); return out; };
const byClass = (root, cls) => walk(root).filter((e) => String(e.className).split(/\s+/).includes(cls));
const menuOn = (body) => byClass(body, 'chat-row-menu')[0] || null;
const itemLabels = (menu) => byClass(menu, 'chat-row-menu-item').map((b) => b.children[0]?.textContent);
const clickItem = (menu, label) => byClass(menu, 'chat-row-menu-item')
  .find((b) => b.children[0]?.textContent === label)
  .fire('click', { stopPropagation() {} });

// One transcript with one settled row carrying `row` as its log record, wired
// with the given hooks; returns what a right-click on the row needs.
const wiredRow = async (row, hooks, tag = '') => {
  const { body } = stubDom();
  const { wireChatRowMenu } = await import(`../js/ui/chatView.js?rowmenu${tag}`);
  const transcript = makeEl();
  const rowEl = makeEl();
  rowEl.className = `chat-msg chat-msg-${row?.role || 'assistant'}`;
  rowEl._chatRow = row;
  transcript.appendChild(rowEl);
  wireChatRowMenu(transcript, hooks);
  const rightClick = () => {
    const ev = { target: rowEl, clientX: 40, clientY: 60, prevented: false,
      preventDefault() { this.prevented = true; }, stopPropagation() {} };
    transcript.fire('contextmenu', ev);
    return ev;
  };
  return { body, transcript, rowEl, rightClick };
};

// ── Items per role ──
test('chatRowMenuItems: every settled row gets Copy / Insert; user rows add Resend', async () => {
  stubDom();
  const { chatRowMenuItems } = await import('../js/ui/chatView.js?rowmenu-items');
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'assistant', text: 'hi' }).map((i) => i.label),
    ['Copy message', 'Insert into prompt'], 'assistant rows never offer Resend');
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'user', text: 'hi' }).map((i) => i.label),
    ['Copy message', 'Insert into prompt', 'Resend']);
  // An in-flight "…" row (and a missing record) gets no menu at all.
  assert.deepStrictEqual(chatRowMenuItems({ role: 'assistant', text: '…', pending: true }), []);
  assert.deepStrictEqual(chatRowMenuItems(null), []);
  // A FAILED turn is settled too — the unreachable card and a Stop both keep the menu
  // (they carry their own Retry beside it, they don't replace it).
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'assistant', text: 'Couldn\'t reach Ollama', error: true, card: true, retryText: 'x' })
      .map((i) => i.label),
    ['Copy message', 'Insert into prompt'], 'the error card keeps its menu');
  assert.deepStrictEqual(
    chatRowMenuItems({ role: 'assistant', text: 'Stopped.', error: true, retryText: 'x' }).map((i) => i.label),
    ['Copy message', 'Insert into prompt'], 'a stopped turn keeps its menu');
});

// ── The trigger yields to the jump pills (the reported "arrows disappear, ⋯ instead") ──
// The pills are the higher-priority control and stay put; a row's "…" lifts clear of
// them, or — a bubble too short to lift it to — hides, rather than the old rule
// (hiding the pills) which the user reported as backwards.
test('rowMenuLiftPx: the lift clears exactly the overlap, plus the gap', async () => {
  stubDom();
  const { rowMenuLiftPx } = await import('../js/ui/chatView.js?rowmenu-jumps');
  // The measured collision from the live repro: the card's trigger under the ⌄ pill.
  const btn = { left: 296, right: 317, top: 181, bottom: 202, width: 21, height: 21 };
  const jumpTop = { left: 263, right: 291, top: 182, bottom: 210, width: 28, height: 28 };
  const jumpBottom = { left: 297, right: 325, top: 182, bottom: 210, width: 28, height: 28 };
  assert.strictEqual(rowMenuLiftPx(btn, [jumpTop, jumpBottom]), Math.ceil(202 - 182) + 6);
  // A user row's trigger sits far left of both — nothing to clear.
  const userBtn = { left: 153, right: 174, top: 181, bottom: 202, width: 21, height: 21 };
  assert.strictEqual(rowMenuLiftPx(userBtn, [jumpTop, jumpBottom]), 0);
  // …and so does a trigger well above the pills' band.
  assert.strictEqual(rowMenuLiftPx({ ...btn, top: 40, bottom: 61 }, [jumpTop, jumpBottom]), 0);
  // Nothing hovered / nothing shown / a collapsed rect: nothing to lift.
  assert.strictEqual(rowMenuLiftPx(null, [jumpBottom]), 0);
  assert.strictEqual(rowMenuLiftPx(btn, []), 0);
  assert.strictEqual(rowMenuLiftPx(btn, [{ left: 297, right: 297, top: 182, bottom: 182, width: 0, height: 0 }]), 0);
});

test('rowMenuLiftFits: only when the lifted trigger stays inside its own row', async () => {
  stubDom();
  const { rowMenuLiftFits } = await import('../js/ui/chatView.js?rowmenu-jumps-fits');
  const row = { top: 100, bottom: 300 };
  const btn = { top: 260, bottom: 281 };
  assert.strictEqual(rowMenuLiftFits(row, btn, 40), true);    // 260-40=220, still >= 100
  assert.strictEqual(rowMenuLiftFits(row, btn, 200), false);  // 260-200=60, above the row's own top
  assert.strictEqual(rowMenuLiftFits(row, btn, 0), true);     // nothing to lift, always fits
  assert.strictEqual(rowMenuLiftFits(null, btn, 40), true);
  assert.strictEqual(rowMenuLiftFits(row, null, 40), true);
});

test('the panel feeds the hovered row\'s trigger to the lift, and a pill hover can\'t hide it', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  const sync = panel.slice(panel.indexOf('const syncRowMenuLift = () => {'), panel.indexOf('const syncJumps = () => {'));
  assert.ok(sync.includes('rowMenuLiftPx(btn, pills)'), 'the pure test decides how far');
  assert.ok(sync.includes('rowMenuLiftFits(hoverRow.getBoundingClientRect(), btn, lift)'),
    'and the pure test decides whether it fits');
  assert.ok(sync.includes("setProperty('--row-menu-lift'"), 'a fit writes the CSS var the trigger reads');
  assert.ok(sync.includes("classList.add('chat-row-menu-yield')"), 'no fit hides it instead');
  // can-up/can-down answer to the popup-open reason only now — never the row overlap.
  const jumpsBody = panel.slice(panel.indexOf('const syncJumps = () => {'),
    panel.indexOf('transcript.addEventListener(\'scroll\', syncJumps'));
  assert.ok(/const up = [^\n]*!standDown;/.test(jumpsBody) && /const down = [^\n]*!standDown;/.test(jumpsBody));
  assert.ok(jumpsBody.includes("classList.toggle('can-up', up)")
    && jumpsBody.includes("classList.toggle('can-down', down)"));
  assert.ok(!jumpsBody.includes('rowMenuLiftPx') && !jumpsBody.includes('rowMenuHitsJumps'),
    'the pills no longer stand down for the row overlap reason');
  // Hover tracking is on the transcript, so a cursor on a pill (a sibling that floats
  // OVER it) leaves no row hovered and the pill survives.
  assert.ok(sync.includes(".chat-row-menu-btn'"), 'measured from the hovered row\'s own trigger');
  assert.ok(panel.includes("transcript.addEventListener('mouseover'"));
  assert.ok(panel.includes("transcript.addEventListener('mouseleave'"));
});

// A LIFTED trigger sits outside its row's own box, so reaching it crosses bare
// transcript background first — a mouseover with no `.chat-msg` at all. Clearing the
// lift on THAT (the earlier bug, user report) snapped the trigger back down and out
// from under the cursor mid-reach; only an actual different row (or a real
// mouseleave) may change what is hovered.
test('reaching a lifted trigger never snaps it back: a no-row mouseover is ignored', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  const over = panel.slice(panel.indexOf("transcript.addEventListener('mouseover'"),
    panel.indexOf("transcript.addEventListener('mouseleave'"));
  assert.match(over, /if \(!row \|\| !transcript\.contains\(row\) \|\| row === hoverRow\) return;/,
    'no row (or the same one) is a no-op — it does NOT fall through to clearing hoverRow');
  // The clear only happens once we know we are switching to a REAL different row.
  const afterGuard = over.slice(over.indexOf('return;') + 'return;'.length);
  assert.ok(afterGuard.includes('clearRowMenuLift(hoverRow)'));
  assert.ok(afterGuard.includes('hoverRow = row;'), 'row is non-null past the guard — no `?? null` needed');
});

// ── Copy ──
test('copyChatText hands the EXACT text to the async clipboard API', async () => {
  stubDom();
  const { copyChatText } = await import('../js/ui/chatView.js?rowmenu-copy');
  const writes = [];
  const nav = { clipboard: { writeText: async (t) => writes.push(t) } };
  const text = 'multi\nline — reply © exact';
  assert.strictEqual(await copyChatText(text, globalThis.document, nav), true);
  assert.deepStrictEqual(writes, [text]);
});

test('copyChatText falls back to execCommand, and reports failure instead of lying', async () => {
  const { body } = stubDom();
  const { copyChatText } = await import('../js/ui/chatView.js?rowmenu-copy2');
  // No async API at all → the hidden-textarea path, with the exact text in it.
  const cmds = [];
  globalThis.document.execCommand = (c) => { cmds.push({ c, v: body.children.at(-1)?.value }); return true; };
  assert.strictEqual(await copyChatText('fallback text', globalThis.document, {}), true);
  assert.deepStrictEqual(cmds, [{ c: 'copy', v: 'fallback text' }]);
  assert.strictEqual(body.children.length, 0, 'the scratch textarea is removed again');
  // A rejecting writeText ALSO falls through to execCommand…
  cmds.length = 0;
  const nav = { clipboard: { writeText: async () => { throw new Error('denied'); } } };
  assert.strictEqual(await copyChatText('second try', globalThis.document, nav), true);
  assert.strictEqual(cmds[0].v, 'second try');
  // …and when even that refuses, the caller hears `false` (that is what toasts).
  globalThis.document.execCommand = () => false;
  assert.strictEqual(await copyChatText('nope', globalThis.document, {}), false);
});

// ── Opening / gating ──
test('right-click on a settled row opens the menu; Copy message copies that row\'s text', async () => {
  const row = { role: 'assistant', text: 'the exact reply' };
  const { body, rightClick } = await wiredRow(row, {}, '-open');
  // Node's own `navigator` has no clipboard, so the menu's copy takes the
  // execCommand fallback — record what lands in the scratch textarea.
  const writes = [];
  globalThis.document.execCommand = () => { writes.push(body.children.at(-1)?.value); return true; };
  const ev = rightClick();
  assert.strictEqual(ev.prevented, true, 'the native menu is replaced');
  const menu = menuOn(body);
  assert.ok(menu, 'the menu floats on the body');
  assert.deepStrictEqual(itemLabels(menu), ['Copy message', 'Insert into prompt']);
  clickItem(menu, 'Copy message');
  await new Promise((r) => setTimeout(r, 0));
  assert.deepStrictEqual(writes, ['the exact reply'], 'the copy callback got the exact text');
  assert.strictEqual(menuOn(body), null, 'an item click closes the menu');
});

test('a pending row, and a right-click on an existing selection, keep the native menu', async () => {
  const { body, rightClick } = await wiredRow({ role: 'assistant', text: '…', pending: true }, {}, '-gate');
  assert.strictEqual(rightClick().prevented, false, 'pending rows offer nothing to act on');
  assert.strictEqual(menuOn(body), null);
  // An existing selection over the row: the NATIVE menu's Copy acts on exactly it.
  const sel = await wiredRow({ role: 'assistant', text: 'done' }, {}, '-gate2');
  globalThis.window.getSelection = () => ({
    isCollapsed: false, toString: () => 'part of it', containsNode: () => true,
  });
  assert.strictEqual(sel.rightClick().prevented, false, 'the selection keeps the native menu');
  assert.strictEqual(menuOn(sel.body), null);
});

// ── The surface hooks ──
test('Insert into prompt hands the row text to the surface\'s composer hook', async () => {
  const inserted = [];
  const row = { role: 'user', text: 'crop 10% off every edge' };
  const { body, rightClick } = await wiredRow(row, { onInsert: (t) => inserted.push(t) }, '-insert');
  rightClick();
  clickItem(menuOn(body), 'Insert into prompt');
  assert.deepStrictEqual(inserted, ['crop 10% off every edge']);
});

test('Resend hands the text AND the row\'s original attachments to the hook', async () => {
  const sent = [];
  const attachments = [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' }];
  const row = { role: 'user', text: 'what is this?', attachments };
  const { body, rightClick } = await wiredRow(row, { onResend: (t, a) => sent.push([t, a]) }, '-resend');
  rightClick();
  const menu = menuOn(body);
  assert.ok(itemLabels(menu).includes('Resend'), 'user rows carry Resend');
  clickItem(menu, 'Resend');
  assert.deepStrictEqual(sent, [['what is this?', attachments]]);
});

test('the menu closes on Escape (and arms outside-press + scroll closers)', async () => {
  const { body, rightClick } = await wiredRow({ role: 'user', text: 'hi' }, {}, '-close');
  rightClick();
  assert.ok(menuOn(body));
  await new Promise((r) => setTimeout(r, 5));   // the closers arm a tick late
  const key = docListeners.find((l) => l.t === 'keydown' && l.cap);
  assert.ok(key, 'Escape closes via a capture listener (it must beat the ctx-menu\'s own)');
  const stops = [];
  key.fn({ key: 'x', stopPropagation: () => stops.push('x') });
  assert.ok(menuOn(body), 'other keys leave it open');
  assert.ok(docListeners.some((l) => l.t === 'pointerdown' && l.cap), 'outside press closes it');
  key.fn({ key: 'Escape', stopPropagation: () => stops.push('esc') });
  assert.strictEqual(menuOn(body), null, 'Escape closes it');
  assert.deepStrictEqual(stops, ['esc'], 'and only Escape is swallowed');
  // …and close() unhooks its document closers — nothing leaks past the menu.
  assert.ok(!docListeners.some((l) => l.t === 'keydown'), 'the keydown closer is removed with the menu');
  assert.ok(!docListeners.some((l) => l.t === 'pointerdown'), 'so is the outside-press closer');
});

// ── The entry pop: the menu grows out of the open point ──
test('menuPopOrigin: the click point relative to the placed box, clamped inside it', async () => {
  const { menuPopOrigin } = await import('../js/ui/motion.js');
  assert.strictEqual(menuPopOrigin(140, 90, { left: 100, top: 60, width: 120, height: 100 }), '40px 30px');
  // A box flipped left/up of the cursor pops from its far corner…
  assert.strictEqual(menuPopOrigin(300, 200, { left: 180, top: 100, width: 120, height: 100 }), '120px 100px');
  // …and a clamp that pushed the box past the click never yields a negative origin.
  assert.strictEqual(menuPopOrigin(2, 3, { left: 8, top: 8, width: 120, height: 100 }), '0px 0px');
});

test('the opened menu carries the click-point transform-origin, and CSS pops it from there', async () => {
  const { body, transcript, rowEl } = await wiredRow({ role: 'user', text: 'hi' }, {}, '-pop');
  // Near the bottom-right edge: the 120x100 stub menu flips left/up of the cursor.
  transcript.fire('contextmenu', { target: rowEl, clientX: 1020, clientY: 700,
    preventDefault() {}, stopPropagation() {} });
  const menu = menuOn(body);
  assert.strictEqual(menu.style.left, '900px');
  assert.strictEqual(menu.style.top, '600px');
  assert.strictEqual(menu.style.transformOrigin, '120px 100px',
    'the origin is the click inside the flipped box — the menu grows out of the cursor');
  // The animation itself is CSS: the shared menuPop keyframe, reduced-motion aware.
  const anims = ANIMATIONS_CSS;
  assert.match(anims, /@keyframes menuPop \{ from \{ opacity: 0; transform: scale\(0\.62\); \}/);
  assert.match(anims, /\.chat-row-menu \{ animation: menuPop 0\.14s ease-out; \}/);
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\s*#ctx-menu\.ctx-open, \.chat-row-menu \{ animation: none; \}/);
});

// ── Resend's requeue mirrors requeueLastTurnAttachments ──
test('requeueRowAttachments refills an EMPTY queue only, capped, as analyze-images', async () => {
  const { requeueRowAttachments } = await import('../js/llm/chatSession.js');
  const { MAX_ATTACHMENTS } = await import('../js/llm/chatController.js');
  const rowAts = [
    { name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' },
    { name: 'clip.mp4', kind: 'video', dataUrl: 'data:image/jpeg;base64,BBB' },   // first frame
    { name: 'broken.png', kind: 'image' },                                        // nothing renderable
  ];
  const ctrl = { attachments: [] };
  assert.strictEqual(requeueRowAttachments(ctrl, rowAts), 2);
  assert.deepStrictEqual(ctrl.attachments, [
    { name: 'cat.jpg', kind: 'image', use: 'analyze', dataUrl: 'data:image/jpeg;base64,AAA' },
    { name: 'clip.mp4', kind: 'image', use: 'analyze', dataUrl: 'data:image/jpeg;base64,BBB' },
  ], 'same shape the controller\'s own requeueLastTurnAttachments pushes');
  // Anything the user queued since WINS — resend must not mix batches.
  const busy = { attachments: [{ name: 'new.png' }] };
  assert.strictEqual(requeueRowAttachments(busy, rowAts), 0);
  assert.strictEqual(busy.attachments.length, 1);
  // And the §7 cap holds.
  const many = Array.from({ length: MAX_ATTACHMENTS + 2 },
    (_, i) => ({ name: `a${i}.png`, kind: 'image', dataUrl: 'data:image/png;base64,AA' }));
  const capped = { attachments: [] };
  assert.strictEqual(requeueRowAttachments(capped, many), MAX_ATTACHMENTS);
  assert.strictEqual(requeueRowAttachments(null, many), 0);
});

// ── Both surfaces wire it, and the chrome matches the app's other row menus ──
test('the panel and the flyout wire the SHARED row menu with insert + resend hooks', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  const menu = contextMenuSource();
  for (const [name, src] of [['panel', panel], ['flyout', menu]]) {
    assert.ok(src.includes('wireChatRowMenu(transcript, {'), `${name} wires the shared menu`);
    assert.ok(src.includes('onInsert: (text) => {'), `${name} passes its composer hook`);
    assert.ok(src.includes('requeueRowAttachments('), `${name}'s Resend re-queues the row attachments`);
    assert.ok(src.includes('runTurn(text).catch('), `${name}'s Resend rides the composer's own send path`);
  }
  // The renderer stamps rows with their log record — that is what the menu reads.
  const view = chatViewSource();
  assert.ok(view.includes('el._chatRow = row;'));
  // Repaints must not tear a live selection out of an unchanged row — and they compare
  // the row's OWN text node, not the whole bubble (the CTA label mismatched forever).
  // …replaced only when the text changed — OR when the row is settling out of its
  // typing dots, which read as '' and so "match" an empty reply (the stuck spinner).
  assert.ok(view.includes('} else if (typing || textEl.textContent !== row.text) {'),
    'text nodes are replaced only when the text changed, dots aside');
  // A row menu open over the flyout counts as "engaged" — hover-out must not close it.
  assert.ok(menu.includes('chatRowMenuOpen()'), 'the flyout keep-open predicate consults it');
});

// ── The hover "…" trigger ──
test('chatRowMenuButton: a trigger per settled row, on the corner facing the panel centre', async () => {
  stubDom();
  const { chatRowMenuButton } = await import('../js/ui/chatView.js?rowmenu-btn');
  const user = chatRowMenuButton({ role: 'user', text: 'hi' });
  assert.ok(String(user.className).split(/\s+/).includes('chat-row-menu-btn'));
  assert.ok(String(user.className).includes('chat-row-menu-btn-left'),
    'user bubbles are right-aligned, so their button sits bottom-LEFT');
  assert.ok(user.innerHTML.includes('ic-more'), 'the shared "⋯" glyph');
  const asst = chatRowMenuButton({ role: 'assistant', text: 'yo' });
  assert.ok(String(asst.className).includes('chat-row-menu-btn-right'),
    'assistant bubbles are left-aligned, so bottom-RIGHT');
  // Pending rows have no menu (chatRowMenuItems is []), so no trigger either.
  assert.strictEqual(chatRowMenuButton({ role: 'assistant', text: '…', pending: true }), null);
  assert.strictEqual(chatRowMenuButton(null), null);
  // renderChatLog appends it per repaint (a text rewrite wipes the row's children).
  const view = chatViewSource();
  assert.ok(view.includes("if (!el.querySelector('.chat-row-menu-btn'))"));
  assert.ok(view.includes('chatRowMenuButton(row)'));
});

test('clicking the "…" button opens the SAME menu as right-click, anchored at the button', async () => {
  const row = { role: 'user', text: 'hello' };
  const { body, transcript, rowEl } = await wiredRow(row, {}, '-btn-open');
  const { chatRowMenuButton } = await import('../js/ui/chatView.js?rowmenu-btn-open');
  const btn = rowEl.appendChild(chatRowMenuButton(row));
  transcript.fire('click', { target: btn, preventDefault() {}, stopPropagation() {} });
  const menu = menuOn(body);
  assert.ok(menu, 'the button opens the floating menu');
  assert.deepStrictEqual(itemLabels(menu), ['Copy message', 'Insert into prompt', 'Resend'],
    'the exact right-click items');
});

// ── Touch gestures (no hover there) ──
test('touchMenuGesture: long-press fires at the threshold; movement or early release cancels', async () => {
  const { touchMenuGesture } = await import('../js/ui/chatView.js?rowmenu-touch');
  const opened = [];
  let armed = null;
  const g = touchMenuGesture((x, y) => opened.push([x, y]), {
    setTimer: (fn, ms) => { armed = { fn, ms }; return 1; },
    clearTimer: () => { armed = null; },
  });
  // Held past the threshold: opens at the touch point, and the release reports it.
  g.start('rowA', 30, 40);
  assert.strictEqual(armed.ms, 500, 'the long-press threshold is 500ms');
  armed.fn();
  assert.deepStrictEqual(opened, [[30, 40]]);
  assert.strictEqual(g.end(0), true, '…so the caller suppresses the native callout');
  // Jitter inside the 10px tolerance keeps it armed; drifting past it cancels.
  opened.length = 0;
  g.start('rowA', 30, 40);
  g.move(35, 44);
  assert.ok(armed, 'sub-tolerance jitter keeps the press armed');
  g.move(30, 60);
  assert.strictEqual(armed, null, 'a drag past 10px is a scroll — press cancelled');
  assert.strictEqual(g.end(10), false);
  // Early release: a plain tap opens nothing.
  g.start('rowB', 1, 2);
  assert.strictEqual(g.end(1000), false);
  assert.deepStrictEqual(opened, []);
});

test('touchMenuGesture: two quick taps on the same row open; slow or cross-row taps don\'t', async () => {
  const { touchMenuGesture } = await import('../js/ui/chatView.js?rowmenu-touch2');
  const opened = [];
  const g = touchMenuGesture((x, y) => opened.push([x, y]), { setTimer: () => 1, clearTimer: () => {} });
  g.start('rowA', 10, 20);
  assert.strictEqual(g.end(100), false, 'the first tap only waits');
  g.start('rowA', 12, 22);
  assert.strictEqual(g.end(300), true, 'a second tap within 350ms opens');
  assert.deepStrictEqual(opened, [[12, 22]], '…at the second tap\'s point');
  // 400ms apart: two singles.
  opened.length = 0;
  g.start('rowA', 0, 0); g.end(1000);
  g.start('rowA', 0, 0);
  assert.strictEqual(g.end(1400), false, 'taps 400ms apart never pair');
  // A tap on ANOTHER row starts over.
  g.start('rowA', 0, 0); g.end(2000);
  g.start('rowB', 0, 0);
  assert.strictEqual(g.end(2100), false, 'cross-row taps never pair');
  assert.deepStrictEqual(opened, []);
});

test('double-tap on a bubble opens the menu through the transcript wiring; pending rows stay inert', async () => {
  const row = { role: 'user', text: 'yo' };
  const { body, transcript, rowEl } = await wiredRow(row, {}, '-touch-wire');
  const tap = (target) => {
    transcript.fire('touchstart', { touches: [{ clientX: 15, clientY: 25 }], target });
    const ev = { prevented: false, preventDefault() { this.prevented = true; } };
    transcript.fire('touchend', ev);
    return ev;
  };
  const first = tap(rowEl);
  assert.strictEqual(menuOn(body), null, 'one tap opens nothing');
  assert.strictEqual(first.prevented, false, '…and native behavior is untouched');
  const second = tap(rowEl);
  assert.ok(menuOn(body), 'the second quick tap opens the menu');
  assert.strictEqual(second.prevented, true, 'only then is the native callout suppressed');
  assert.deepStrictEqual(itemLabels(menuOn(body)), ['Copy message', 'Insert into prompt', 'Resend']);
  // A pending row never opens, however many taps land on it.
  const pend = await wiredRow({ role: 'assistant', text: '…', pending: true }, {}, '-touch-pend');
  const tapPend = () => {
    pend.transcript.fire('touchstart', { touches: [{ clientX: 5, clientY: 5 }], target: pend.rowEl });
    pend.transcript.fire('touchend', { preventDefault() {} });
  };
  tapPend(); tapPend();
  assert.strictEqual(menuOn(pend.body), null);
});

// ── The chrome deltas ──
test('the chat menu hugs its content, and the "…" trigger is hover-gated CSS', () => {
  const css = COMPONENTS_CSS;
  const shared = css.indexOf('.project-menu, .chat-row-menu {');
  const override = css.indexOf('.chat-row-menu { min-width: 0; }');
  assert.ok(shared > -1 && override > shared,
    'the chat menu sheds the shared 184px floor AFTER the aliased rule (projects keep it)');
  assert.ok(css.includes('.chat-row-menu-item { padding: 7px 9px; }'), 'tighter items, chat only');
  // Per-side placement classes exist, and the reveal lives behind the hover guard.
  assert.ok(css.includes('.chat-row-menu-btn-left { left:'));
  assert.ok(css.includes('.chat-row-menu-btn-right { right:'));
  assert.ok(/@media \(hover: hover\)[^]*?\.chat-msg:hover \.chat-row-menu-btn/.test(css),
    'touch surfaces never see the hover trigger');
  // Anchored without moving the bubble, and never part of a text selection.
  const msg = css.slice(css.indexOf('\n.chat-msg {'), css.indexOf('}', css.indexOf('\n.chat-msg {')));
  assert.ok(msg.includes('position: relative'));
  const btn = css.slice(css.indexOf('.chat-row-menu-btn {'), css.indexOf('.chat-row-menu-btn-left'));
  assert.ok(btn.includes('position: absolute') && btn.includes('user-select: none'));
});

test('the row menu wears the projects row menu\'s exact chrome, and messages stay selectable', () => {
  const css = COMPONENTS_CSS;
  // Aliased selectors, not a copied block — the two menus can never drift apart.
  assert.ok(css.includes('.project-menu, .chat-row-menu {'), 'one floating-menu block');
  assert.ok(css.includes('.project-menu-item, .chat-row-menu-item {'), 'one item treatment');
  assert.ok(css.includes('.project-menu-item:hover, .chat-row-menu-item:hover'), 'same hover accent');
  // Theme vars only — both themes follow automatically.
  const block = css.slice(css.indexOf('.project-menu, .chat-row-menu {'), css.indexOf('.confirm-choose-row'));
  assert.ok(/var\(--bg-container\)/.test(block) && /var\(--accent\)/.test(block) && /var\(--text-main\)/.test(block));
  // The selection fix: message bubbles opt into selection EXPLICITLY (the ctx-menu
  // flyout lives inside a user-select:none ancestor).
  const msg = css.slice(css.indexOf('\n.chat-msg {'), css.indexOf('}', css.indexOf('\n.chat-msg {')));
  assert.ok(msg.includes('user-select: text'), 'message text is selectable');
  assert.ok(msg.includes('-webkit-user-select: text'), '…including WebKit');
});

// ── An open menu owns the corner the jump pills float in ────────────────────
// Reported: the ⌃/⌄ pills rendered over the open row menu. Two independent guarantees,
// because one number in a stylesheet is not a fix: the menu STACKS above them, and the
// pills stand down for as long as any menu is open.
test('the row menu shares the app popup tier, which is above the panel and its pills', () => {
  const css = COMPONENTS_CSS;
  const tier = (re) => { const m = re.exec(css); return m ? Number(m[1]) : null; };
  // The chat menu is ALIASED onto the projects row menu — same block, same level.
  const shared = /\.project-menu, \.chat-row-menu \{[^}]*z-index: (\d+)/.exec(css);
  assert.ok(shared, 'the two row menus share one block');
  const menuZ = Number(shared[1]);
  // …the same tier the app's other popups use, not an ad-hoc number.
  assert.strictEqual(menuZ, tier(/#app-tooltip \{[^}]*z-index: (\d+)/), 'same tier as the tooltip');
  assert.strictEqual(menuZ, tier(/#confirm-modal-overlay\.modal-open \{ z-index: (\d+); \}/),
    'same tier as the confirm dialog');
  // The pills live INSIDE the chat panel, so the panel's own level is what the menu has
  // to clear — and it does, by three orders of magnitude.
  const panelZ = tier(/stencil-chat-panel \{[^}]*z-index: (\d+)/);
  const jumpsZ = tier(/\.chat-jumps \{[^}]*z-index: (\d+)/);
  assert.ok(panelZ && jumpsZ);
  assert.ok(menuZ > panelZ, `menu ${menuZ} must outrank the panel ${panelZ}`);
  assert.ok(panelZ > jumpsZ, 'the pills are scoped inside the panel, so the panel is the ceiling');
  // Fullscreen chrome is lower still, so the menu wins there too.
  for (const m of css.matchAll(/z-index: (100\d\d);/g)) assert.ok(Number(m[1]) < menuZ + 8);
  assert.ok(menuZ > 10002, 'above every fullscreen panel (10000-10002)');
});

test('a chat popup is announced on BOTH edges, and the pills stand down while it is open', async () => {
  stubDom();
  const { CHAT_POPUP_EVENT, chatPopupOpen, chatRowMenuOpen } = await import('../js/ui/chatRowMenu.js?menu-events');
  assert.strictEqual(CHAT_POPUP_EVENT, 'stencil:chat-popup');
  assert.strictEqual(chatPopupOpen(), false, 'nothing open to begin with');
  assert.strictEqual(chatRowMenuOpen(), false);
  const view = chatViewSource();
  // Announced when it opens…
  const open = view.slice(view.indexOf('const openChatRowMenu ='));
  assert.ok(/rowMenuEl = menu;\s*\n\s*rowMenuClose = close;\s*\n\s*announcePopup\(\);/.test(open),
    'the open path announces after the menu is live');
  // …and when it closes, AFTER rowMenuEl is cleared so a listener reads "closed".
  const close = view.slice(view.indexOf('  const close = () => {', view.indexOf('const openChatRowMenu =')));
  const clearAt = close.indexOf('rowMenuEl = null;');
  const announceAt = close.indexOf('announcePopup();');
  assert.ok(clearAt > -1 && announceAt > clearAt, 'closed state is visible before the event fires');
  // The panel reacts to both edges: an open popup stands the PILLS down (they are
  // never hidden by the row-overlap reason any more — that yields the TRIGGER instead).
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  assert.ok(panel.includes('const standDown = chatPopupOpen();'));
  assert.ok(panel.includes('subscribe(CHAT_POPUP_EVENT, syncJumps);'));
  // …and standDown gates BOTH classes, so neither arrow can survive an open menu.
  assert.ok(/const up = [^\n]*!standDown;/.test(panel) && /const down = [^\n]*!standDown;/.test(panel));
  // Nothing latches: the only inputs are the live menu/pill state and the hovered
  // trigger, so a close restores whatever the scroll position deserves.
  const sync = panel.slice(panel.indexOf('const syncRowMenuLift = () => {'), panel.indexOf('transcript.addEventListener(\'mouseover\''));
  assert.ok(!/menuWasOpen|pillsHidden|wasStandDown/.test(sync),
    'no remembered hidden state to get stuck in — it is recomputed every time');
});

test('the flyout shares the one menu, so its menu moves the panel pills too', () => {
  const menu = contextMenuSource();
  const view = chatViewSource();
  // One module-level menu for both surfaces (the flyout's keep-open predicate reads it),
  // and the announcement is inside that shared open/close — not in a per-surface wrapper.
  assert.ok(view.includes('let rowMenuEl = null;'), 'one menu app-wide');
  assert.ok(menu.includes('chatRowMenuOpen()'), 'the flyout consults the same state');
  // Two edges for the row menu, and the composer menu funnels both of its own through
  // one setOpen — so every popup edge in the module announces.
  assert.strictEqual((view.match(/announcePopup\(\)/g) || []).length, 3);
  // The pills only exist in the panel, so the panel is the only listener needed.
  assert.ok(!/chat-jumps/.test(menu), 'the flyout has no pills of its own');
});

// ── The composer's "…" menu (Add image / Clear history / Settings) ──────────
// Reported: the ⌃ pill drew straight over it, next to "Add image". Unlike the row menu
// this one is IN-PANEL — absolute inside the composer, popping upward into the pills'
// corner — so it competes with them directly and lost at z-index 5 vs 6.
test('the composer overflow menu outranks the jump pills in the panel\'s own stacking', () => {
  const css = COMPONENTS_CSS;
  const lvl = (re) => { const m = re.exec(css); return m ? Number(m[1]) : null; };
  const menuZ = lvl(/\.chat-more-menu \{[^}]*z-index: (\d+)/);
  const jumpsZ = lvl(/\.chat-jumps \{[^}]*z-index: (\d+)/);
  const cueZ = lvl(/\.chat-drop-cue \{[^}]*z-index: (\d+)/);
  assert.ok(menuZ && jumpsZ && cueZ);
  assert.ok(menuZ > jumpsZ, `the popup ${menuZ} must cover the affordance ${jumpsZ}`);
  assert.ok(menuZ > cueZ, 'and the composer drop cue');
  // It pops UPWARD out of the composer — which is why it reaches the pills at all.
  assert.match(css, /\.chat-more-menu \{[^}]*bottom: calc\(100% \+ 6px\)/);
  // Its wrap is the positioning context, so the level is scoped to the panel.
  assert.match(css, /\.chat-more-wrap \{ position: relative;/);
});

test('the composer menu joins the popup accounting on both edges', async () => {
  const view = chatViewSource();
  // ONE toggle path, so "is it open" can never disagree with what is on screen.
  const wire = view.slice(view.indexOf('export const wireChatMoreMenu'), view.indexOf('export const chatComposerActionsHtml') + 1 || undefined);
  const body = view.slice(view.indexOf('export const wireChatMoreMenu'));
  assert.ok(body.includes('const setOpen = (on) => {'), 'both edges funnel through setOpen');
  assert.ok(body.includes('if (on) openComposerMenus.add(menu); else openComposerMenus.delete(menu);'));
  assert.ok(body.includes('const close = () => setOpen(false);'));
  assert.ok(body.includes('setOpen(menu.hidden);'), 'the toggle goes through it too');
  // Every close path — item click, outside pointerdown, Escape — is that same close.
  assert.ok(body.includes("for (const item of menu.querySelectorAll('.chat-more-item')) item.addEventListener('click', close);"));
  assert.ok(/pointerdown[\s\S]{0,120}close\(\)/.test(body));
  assert.ok(/Escape[\s\S]{0,60}close\(\)/.test(body));
  // …and the pills read one predicate covering row menu AND composer menus.
  assert.match(view, /export const chatPopupOpen = \(\) => !!rowMenuEl \|\| openComposerMenus\.size > 0;/);
  // Both surfaces wire a composer menu, so both feed the same accounting.
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  const flyout = contextMenuSource();
  assert.match(panel, /wireChatMoreMenu\('chat'/);
  assert.match(flyout, /wireChatMoreMenu\('ctx-assist'/);
});

// ── The audit: every popup that can cover the chat panel, and its level ─────
// So we stop finding these one screenshot at a time — a new popup has to be added here
// with a declared level, or this fails.
test('every chat-panel popup declares a level that clears the pills', () => {
  const css = COMPONENTS_CSS;
  const lvl = (re) => { const m = re.exec(css); return m ? Number(m[1]) : null; };
  const PANEL = lvl(/stencil-chat-panel \{[^}]*z-index: (\d+)/);
  const JUMPS = lvl(/\.chat-jumps \{[^}]*z-index: (\d+)/);
  // scope: 'body'  → its own stacking context above the panel; compare against PANEL
  // scope: 'panel' → inside the panel's context; compare against JUMPS
  const AUDIT = [
    { name: '.chat-row-menu (per-message ⋯ / right-click)', scope: 'body', z: lvl(/\.project-menu, \.chat-row-menu \{[^}]*z-index: (\d+)/) },
    { name: '.chat-status-tip (provider tooltip)', scope: 'body', z: lvl(/\.chat-status-tip \{[^}]*z-index: (\d+)/) },
    { name: '.chat-thumb-preview (attachment glance)', scope: 'body', z: lvl(/\.chat-thumb-preview \{[^}]*z-index: (\d+)/) },
    { name: '#app-tooltip (shared instant tooltip)', scope: 'body', z: lvl(/#app-tooltip \{[^}]*z-index: (\d+)/) },
    { name: '.chat-more-menu (composer ⋯)', scope: 'panel', z: lvl(/\.chat-more-menu \{[^}]*z-index: (\d+)/) },
    { name: '.accent-dd-menu.dd-portal (select dropdowns, portaled to body)', scope: 'body', z: lvl(/\.accent-dd-menu\.dd-portal \{[^}]*z-index: (\d+)/) },
  ];
  for (const p of AUDIT) {
    assert.ok(Number.isFinite(p.z), `${p.name} must declare a z-index`);
    if (p.scope === 'body') assert.ok(p.z > PANEL, `${p.name} (${p.z}) must clear the panel (${PANEL})`);
    else assert.ok(p.z > JUMPS, `${p.name} (${p.z}) must clear the jump pills (${JUMPS})`);
  }
  // The affordances that must stay UNDER every popup above.
  for (const [name, z] of [['.chat-jumps', JUMPS], ['.chat-drop-cue', lvl(/\.chat-drop-cue \{[^}]*z-index: (\d+)/)]]) {
    assert.ok(z < lvl(/\.chat-more-menu \{[^}]*z-index: (\d+)/), `${name} stays below in-panel popups`);
  }
  // Ask cards / result cards / attachment chips are flow content — no z-index, so they
  // can never join this contest. If one ever gains one, this catches it.
  for (const sel of ['.chat-ask {', '.chat-results {', '.chat-attach-chip {']) {
    const at = css.indexOf(sel);
    if (at === -1) continue;
    assert.ok(!/z-index/.test(css.slice(at, css.indexOf('}', at))), `${sel} must stay flow content`);
  }
});
