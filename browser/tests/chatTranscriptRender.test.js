import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  resetChatLog, chatLog, runLoggedChatTurn, chatTurnInFlight,
} from '../js/llm/chatSession.js';
import {
  revealFeather, REVEAL_FEATHER, revealVisibleBottom, revealTriggerFits,
  REVEAL_SMOOTH_FEATHER_PX, REVEAL_TRIGGER_SIZE, REVEAL_TRIGGER_PAD, dustFitsScroller,
  chatArrivalPoint, CHAT_ENTER_REACH,
} from '../js/ui/motion.js';
import { shrinkWrapWidth } from '../js/ui/chatView.js';

// ── A DOM-lite live tree, enough to actually RUN renderChatLog ───────────────
// The other chat views are pinned against their source; the bugs this file guards
// are structural (a bubble holding its text twice, a row missing its "…" trigger),
// so they have to be observed on a real tree rather than read out of the file.
const matchesSel = (el, sel) => {
  if (sel.startsWith('.')) return el.classList.contains(sel.slice(1));
  const m = /^\[([\w-]+)(?:="([^"]*)")?\]$/.exec(sel);
  if (!m) return false;
  const key = m[1].replace(/^data-/, '').replace(/-(\w)/g, (_, c) => c.toUpperCase());
  const v = el.dataset[key];
  return m[2] === undefined ? v !== undefined : String(v) === m[2];
};
const descendants = (el, out = []) => {
  for (const c of el.children) { out.push(c); descendants(c, out); }
  return out;
};

const makeEl = (tag = 'div') => {
  const classes = new Set();
  const el = {
    tagName: String(tag).toUpperCase(), children: [], parentNode: null,
    // dataset stringifies, like the real one — renderChatLog compares its keys as strings.
    dataset: new Proxy({}, { set: (t, k, v) => { t[k] = String(v); return true; } }),
    _text: '', innerHTML: '', title: '', type: '', tabIndex: 0,
    scrollTop: 0, scrollHeight: 0, clientHeight: 0,
    style: { setProperty() {}, getPropertyValue() { return ''; } },
    get className() { return [...classes].join(' '); },
    set className(v) { classes.clear(); for (const c of String(v).split(/\s+/)) if (c) classes.add(c); },
    classList: {
      add: (...cs) => cs.forEach((c) => classes.add(c)),
      remove: (...cs) => cs.forEach((c) => classes.delete(c)),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
      contains: (c) => classes.has(c),
    },
    set textContent(v) { el._text = String(v); el.children.length = 0; },
    get textContent() { return el._text + el.children.map((c) => c.textContent).join(''); },
    setAttribute() {}, removeAttribute(name) { delete el.dataset[name.replace(/^data-/, '')]; },
    addEventListener(t, fn) { (el._l ||= {})[t] = [...(el._l?.[t] || []), fn]; },
    removeEventListener() {},
    fire(t, ev = {}) { for (const fn of el._l?.[t] || []) fn(ev); },
    appendChild(c) { if (c.parentNode) c.remove(); c.parentNode = el; el.children.push(c); return c; },
    append(...cs) { for (const c of cs) el.appendChild(c); },
    prepend(c) { c.parentNode = el; el.children.unshift(c); return c; },
    before(node) { const k = el.parentNode.children; k.splice(k.indexOf(el), 0, node); node.parentNode = el.parentNode; },
    after(node) { const k = el.parentNode.children; k.splice(k.indexOf(el) + 1, 0, node); node.parentNode = el.parentNode; },
    remove() {
      const k = el.parentNode?.children;
      const i = k ? k.indexOf(el) : -1;
      if (i >= 0) k.splice(i, 1);
      el.parentNode = null;
    },
    contains(n) { return n === el || descendants(el).includes(n); },
    closest(sel) {
      for (let n = el; n; n = n.parentNode) if (matchesSel(n, sel)) return n;
      return null;
    },
    querySelector(sel) { return descendants(el).find((d) => matchesSel(d, sel)) || null; },
    querySelectorAll(sel) { return descendants(el).filter((d) => matchesSel(d, sel)); },
  };
  return el;
};

const stubDom = () => {
  globalThis.document = { createElement: makeEl, body: makeEl('body'), getElementById: () => null,
    addEventListener() {}, removeEventListener() {} };
  globalThis.window = { innerWidth: 1024, innerHeight: 768, addEventListener() {}, removeEventListener() {} };
};

const rowsOf = (transcript) => transcript.children.filter((c) => c.classList.contains('chat-msg'));
const textNodesOf = (rowEl) => descendants(rowEl).filter((d) => d.classList.contains('chat-msg-text'));

// ── The reported bug: a bubble showing its prompt twice ─────────────────────
const PROMPT = 'upload in incognito mode, bame b&w, crop to portrait, turn on horz comparison';

test('a bubble renders its text exactly ONCE, however often the log repaints', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chatView.js?render-once');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: PROMPT },
    { id: 2, role: 'assistant', text: '…', pending: true },
  ];
  const hooks = { onConfigure() {}, onRetry() {} };
  renderChatLog(transcript, log, hooks);
  // The turn fails: the pending row resolves into the unreachable CARD, which is the
  // row that used to be torn down and rebuilt on every single repaint.
  Object.assign(log[1], { pending: false, text: 'Couldn\'t reach it', error: true, card: true, retryText: PROMPT });
  for (let i = 0; i < 5; i++) renderChatLog(transcript, log, hooks);

  const [user, card] = rowsOf(transcript);
  assert.strictEqual(rowsOf(transcript).length, 2, 'two bubbles, not four');
  // ONE text node per row, and it holds the message once — no second copy stacked
  // beside the row's buttons.
  assert.strictEqual(textNodesOf(user).length, 1, 'the user bubble has one text node');
  assert.strictEqual(textNodesOf(card).length, 1);
  assert.strictEqual(user.textContent.split(PROMPT).length - 1, 1, 'the prompt appears exactly once');
  assert.strictEqual(user.textContent, PROMPT, 'and nothing else leaks into the bubble');
  // The affordances are built once too — five repaints must not stack five buttons.
  assert.strictEqual(descendants(card).filter((d) => d.classList.contains('chat-retry-cta')).length, 1);
  assert.strictEqual(descendants(card).filter((d) => d.classList.contains('chat-config-cta')).length, 1);
  // …and the column class the Retry needs survives the wholesale className rewrite.
  assert.ok(card.classList.contains('chat-msg-cta'), 'the retry row stays a flex column');
});

test('the "…" trigger exists on EVERY settled row, the first/oldest included', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chatView.js?render-menu');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: 'first' },
    { id: 2, role: 'assistant', text: 'reply' },
    { id: 3, role: 'user', text: 'second' },
    { id: 4, role: 'assistant', text: '…', pending: true },
  ];
  renderChatLog(transcript, log, {});
  const rows = rowsOf(transcript);
  const trigger = (r) => descendants(r).filter((d) => d.classList.contains('chat-row-menu-btn'));
  assert.deepStrictEqual(rows.slice(0, 3).map((r) => trigger(r).length), [1, 1, 1],
    'the oldest row gets one just like the newest');
  assert.strictEqual(trigger(rows[3]).length, 0, 'an in-flight row has no menu to open');
  // It settles → it gets one, and a repaint never adds a second.
  Object.assign(log[3], { pending: false, text: 'done' });
  renderChatLog(transcript, log, {});
  renderChatLog(transcript, log, {});
  assert.deepStrictEqual(rowsOf(transcript).map((r) => trigger(r).length), [1, 1, 1, 1]);
  // The trigger sits on the corner facing the panel centre, per role.
  assert.ok(trigger(rows[0])[0].classList.contains('chat-row-menu-btn-left'));
  assert.ok(trigger(rows[1])[0].classList.contains('chat-row-menu-btn-right'));
});

test('a FAILED turn — error card or Stop — gets the "…" too, beside its own Retry', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chatView.js?render-menu-error');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: 'crop to portrait' },
    // The reported card: unreachable provider → message + configure CTA + Retry.
    { id: 2, role: 'assistant', text: 'Ollama at localhost:11434 answered: model is required',
      error: true, card: true, retryText: 'crop to portrait' },
    { id: 3, role: 'assistant', text: 'Stopped.', error: true, retryText: 'crop to portrait' },
  ];
  const hooks = { onConfigure() {}, onRetry() {} };
  renderChatLog(transcript, log, hooks);
  renderChatLog(transcript, log, hooks);   // a repaint must not stack a second one
  const rows = rowsOf(transcript);
  const has = (r, cls) => descendants(r).filter((d) => d.classList.contains(cls));
  assert.deepStrictEqual(rows.map((r) => has(r, 'chat-row-menu-btn').length), [1, 1, 1],
    'the error card and the stopped row are settled rows like any other');
  // Assistant side, so the trigger faces the panel centre from the RIGHT.
  assert.ok(has(rows[1], 'chat-row-menu-btn')[0].classList.contains('chat-row-menu-btn-right'));
  assert.ok(has(rows[2], 'chat-row-menu-btn')[0].classList.contains('chat-row-menu-btn-right'));
  // …and the card's own controls are still there beside it (the menu replaces nothing).
  assert.strictEqual(has(rows[1], 'chat-retry-cta').length, 1);
  assert.strictEqual(has(rows[1], 'chat-config-cta').length, 1);
  assert.strictEqual(has(rows[2], 'chat-retry-cta').length, 1, 'a Stop is retryable too');
  assert.strictEqual(has(rows[2], 'chat-config-cta').length, 0, 'but it is no provider card');
});

// ── The stuck spinner: a settled row must NEVER keep the typing dots ────────
// The dots are three EMPTY <i>s, so a pending bubble's text node reads as ''. Writing
// the text "only when it changed" therefore skipped the settle of a turn whose reply
// was '' — the work had run, the composer freed itself, and the bubble span forever
// (user report: the pipeline did everything, the chat never answered).
test('a turn that settles with an EMPTY reply still drops its typing indicator', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chatView.js?render-empty-settle');
  const transcript = makeEl();
  const log = [{ id: 1, role: 'assistant', text: '…', pending: true }];
  renderChatLog(transcript, log, {});
  const row = rowsOf(transcript)[0];
  const dots = () => descendants(row).filter((d) => d.classList.contains('chat-typing')).length;
  assert.strictEqual(dots(), 1, 'an in-flight turn spins');
  // It settles with NOTHING to say — the exact shape a blank model answer produces.
  Object.assign(log[0], { pending: false, text: '' });
  renderChatLog(transcript, log, {});
  assert.strictEqual(dots(), 0, 'the settled row stopped spinning');
  assert.strictEqual(row.textContent, '', 'and holds its (empty) reply');
  // …and the menu the row now deserves is there, since it is a settled row.
  assert.strictEqual(descendants(row).filter((d) => d.classList.contains('chat-row-menu-btn')).length, 1);
});

test('a blank model answer becomes WORDS, not an empty bubble', async () => {
  const { settledReplyText, EMPTY_REPLY_TEXT, replyWithWarnings } =
    await import('../js/llm/chatSession.js');
  // Nothing at all to show → say so; anything else is passed straight through.
  assert.strictEqual(settledReplyText({ reply: '', warnings: [] }), EMPTY_REPLY_TEXT);
  assert.strictEqual(settledReplyText({ reply: '   ' }), EMPTY_REPLY_TEXT);
  assert.strictEqual(settledReplyText(null), EMPTY_REPLY_TEXT);
  assert.strictEqual(settledReplyText({ reply: 'Done.' }), 'Done.');
  // A warning alone is still an answer — shown without the empty reply's blank line.
  assert.strictEqual(settledReplyText({ reply: '', warnings: ['loaded it'] }), '(loaded it)');
  assert.strictEqual(replyWithWarnings({ reply: '', warnings: ['loaded it'] }), '\n(loaded it)');
});

test('nothing can leave a row pending: the turn settles it in a finally', async () => {
  resetChatLog();
  // The answer arrives, then the surface's own onResult throws — the row must not be
  // left spinning by someone else's bug.
  const controller = { attachments: [], async send() { return { reply: 'Done.', warnings: [] }; } };
  await assert.rejects(() => runLoggedChatTurn(controller, 'go', {
    onResult() { throw new Error('surface blew up'); },
  }));
  const rows = chatLog();
  assert.strictEqual(rows.at(-1).pending, false, 'the row is settled whatever happened');
  assert.strictEqual(chatTurnInFlight(), false, 'and the shared flag is clear');
  resetChatLog();
});

// ── Retry: one turn per click, logged once ──────────────────────────────────
const failingController = () => ({
  attachments: [],
  sent: [],
  async send(text) { this.sent.push(text); throw new Error('LLM request failed'); },
});

test('retrying a failed turn logs a NEW turn — it never doubles an existing bubble', async () => {
  resetChatLog();
  const controller = failingController();
  await runLoggedChatTurn(controller, PROMPT);
  await runLoggedChatTurn(controller, PROMPT);   // the Retry button's exact path
  const rows = chatLog();
  assert.deepStrictEqual(rows.map((r) => r.role), ['user', 'assistant', 'user', 'assistant']);
  // Each user row carries the prompt ONCE — no retry appended it to the row before it.
  for (const r of rows.filter((x) => x.role === 'user')) {
    assert.strictEqual(r.text, PROMPT);
    assert.strictEqual(r.text.split(PROMPT).length - 1, 1);
  }
  assert.deepStrictEqual(controller.sent, [PROMPT, PROMPT]);
  assert.ok(rows[3].error && rows[3].retryText === PROMPT, 'the failed row still offers its retry');
  resetChatLog();
});

test('ONE turn at a time: the shared in-flight flag every surface reads', async () => {
  resetChatLog();
  assert.strictEqual(chatTurnInFlight(), false);
  let seenDuringSend = null;
  const controller = {
    attachments: [],
    async send() { seenDuringSend = chatTurnInFlight(); throw new Error('nope'); },
  };
  const run = runLoggedChatTurn(controller, PROMPT, {
    // cleanup runs on the way out and may start the next turn — the flag is already down.
    cleanup: () => { assert.strictEqual(chatTurnInFlight(), false); },
  });
  await run;
  assert.strictEqual(seenDuringSend, true, 'a turn in flight is visible to every surface');
  assert.strictEqual(chatTurnInFlight(), false, 'and cleared once it lands');
  assert.strictEqual(chatLog().length, 2, 'exactly one turn was logged');
  resetChatLog();

  // Both retry/resend entry points consult it, so a click in one surface cannot start
  // a second turn over a turn the OTHER surface is running (that is what logged the
  // user's prompt twice).
  for (const [name, path] of [['panel', '../js/ui/chatPanel.js'], ['flyout', '../js/ui/contextMenu.js']]) {
    const src = readFileSync(new URL(path, import.meta.url), 'utf8');
    assert.ok(src.includes('chatTurnInFlight()'), `${name} guards on the shared flag`);
    assert.strictEqual(src.split('chatTurnInFlight()').length - 1, 2,
      `${name} guards BOTH retry and resend`);
  }
});

// ── The scroll-reveal mask must only sand the edge that is actually cut ─────
test('revealFeather softens only the edge the scroller is really clipping', () => {
  const H = 500;
  // Fully visible: nothing to soften at either end.
  assert.deepStrictEqual(revealFeather(20, 200, H), { in: '0%', out: '0%' });
  // Clipped at the TOP only — the bottom is on screen, so it stays hard. This is the
  // ghost-text bug: the bottom 10% of the top message was sanded into a speckled
  // second copy of its last line, and it swallowed the "…" trigger sitting there.
  assert.deepStrictEqual(revealFeather(-120, 26, H), { in: REVEAL_FEATHER, out: '0%' });
  // Clipped at the BOTTOM only.
  assert.deepStrictEqual(revealFeather(400, 600, H), { in: '0%', out: REVEAL_FEATHER });
  // Taller than the scroller: both ends are genuinely cut.
  assert.deepStrictEqual(revealFeather(-50, 900, H), { in: REVEAL_FEATHER, out: REVEAL_FEATHER });
  // Flush with an edge is NOT clipped (half-pixel slack).
  assert.deepStrictEqual(revealFeather(0, H, H), { in: '0%', out: '0%' });
  assert.deepStrictEqual(revealFeather(-0.4, H + 0.4, H), { in: '0%', out: '0%' });
});

test('the mask wipe reads those feathers, and the observer publishes them', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
    css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  // Both spellings of the mask (the -webkit- one included) take the widths as vars.
  assert.strictEqual(rest.split('calc(var(--vis-start, 0%) + var(--fade-in, 10%))').length - 1, 2);
  assert.strictEqual(rest.split('calc(var(--vis-end, 100%) - var(--fade-out, 10%))').length - 1, 2);
  // A hard-coded 10% either side is exactly the bug — it faded edges nothing was cutting.
  assert.ok(!/\+ 10%\)/.test(rest) && !/- 10%\)/.test(rest), 'no unconditional feather is left');
  const base = css.slice(css.indexOf('.reveal-item {'), css.indexOf('\n}', css.indexOf('.reveal-item {')));
  assert.ok(/--fade-in: 10%/.test(base) && /--fade-out: 10%/.test(base), 'the entering state softens both ways');
  const motion = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  assert.ok(motion.includes("row.el.style.setProperty('--fade-in', fade.in);"));
  assert.ok(motion.includes("row.el.style.setProperty('--fade-out', fade.out);"));
});

test('a repaint keeps the motion classes motion.js owns, mask state included', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chatView.js?render-motion');
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  // Taken from motion.js's constants — a retyped list is what dropped .reveal-masked,
  // and the observer's "changed?" cache then never put it back.
  assert.ok(view.includes('const MOTION_CLASSES = [REVEAL_ITEM_CLASS, REVEAL_IN_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS,\n  REVEAL_SMOOTH_CLASS, REVEAL_NO_TRIGGER_CLASS, CHAT_ENTERING_CLASS];'));
  const transcript = makeEl();
  const log = [{ id: 1, role: 'user', text: 'hi' }];
  renderChatLog(transcript, log, {});
  const row = rowsOf(transcript)[0];
  for (const c of ['reveal-item', 'reveal-masked', 'reveal-entering', 'reveal-smooth', 'reveal-no-trigger',
                   'chat-entering']) row.classList.add(c);
  log[0].text = 'hi there';
  renderChatLog(transcript, log, {});
  // …the smooth-fade and no-trigger flags included: the observer only writes them when
  // they CHANGE, so a repaint that dropped one left the row grainy (or its "…" hidden)
  // until the next threshold crossing.
  for (const c of ['reveal-item', 'reveal-masked', 'reveal-entering', 'reveal-smooth', 'reveal-no-trigger']) {
    assert.ok(row.classList.contains(c), `${c} survives the repaint`);
  }
  // …and the arrival VEIL above all: one turn appends two rows (the message, then the
  // pending "…"), so the second append repaints the first. Dropping it here tore the veil
  // off a frame after it went on, and the user's own bubble appeared instantly while its
  // dust was still flying — the bug reported on Retry.
  assert.ok(row.classList.contains('chat-entering'), 'the arrival veil survives the repaint');
  assert.ok(row.classList.contains('chat-msg-user'), 'and the row still carries its own classes');
});

// ── The "…" trigger must be REACHABLE on any row you can see at all ────────
// It is bottom-anchored inside the row, so `bottom: 0` parked it below the fold on
// every row the scroller cut at the bottom. It now rides --visible-bottom, which
// motion.js publishes from the same cached geometry that drives the mask.
const BTN = 21;   // .chat-row-menu-btn is 21x21 (pinned below)
// The button's box and the row's on-screen slice, both in row-local coordinates.
const buttonBox = (top, h, viewH) => {
  const bottom = h - revealVisibleBottom(top, top + h, viewH);
  return { top: bottom - BTN, bottom };
};
const visibleSlice = (top, h, viewH) => ({ top: Math.max(0, -top), bottom: Math.min(h, viewH - top) });
const overlap = (a, b) => Math.max(0, Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top));

test('the "…" trigger lands inside the row ∩ viewport, whichever edge is cut', () => {
  const H = 500;
  const cases = [
    ['fully visible', 40, 120],
    ['clipped at the top', -120, 146],
    ['a sliver left at the bottom of the top row', -141, 146],
    ['clipped at the bottom', 430, 146],
    ['a sliver peeking in at the bottom', 495, 146],
    ['taller than the whole scroller', -50, 900],
  ];
  for (const [name, top, h] of cases) {
    const box = buttonBox(top, h, H);
    const slice = visibleSlice(top, h, H);
    assert.ok(overlap(box, slice) > 0, `${name}: some of the trigger is on screen`);
    assert.ok(box.bottom <= slice.bottom + 0.001, `${name}: it never hangs below the visible slice`);
    assert.ok(box.bottom >= slice.top, `${name}: nor floats above it`);
    // …and in the scroller's own coordinates it is inside the viewport.
    assert.ok(top + box.bottom >= 0 && top + box.bottom <= H, `${name}: inside the viewport`);
  }
  // A row nothing of which is on screen keeps the plain anchor — there is nothing to
  // clamp to, and it costs no work.
  assert.strictEqual(revealVisibleBottom(600, 720, H), 0, 'wholly below the fold');
  assert.strictEqual(revealVisibleBottom(-300, -100, H), 0, 'wholly above it');
  assert.strictEqual(revealVisibleBottom(10, 10, H), 0, 'an empty row');
});

test('revealVisibleBottom clears a CONSTANT fade band, but never leaves the visible slice', () => {
  const H = 500;
  // Nothing cut at the bottom ⇒ the trigger stays exactly on the row's own edge.
  assert.strictEqual(revealVisibleBottom(40, 160, H), 0);
  assert.strictEqual(revealVisibleBottom(-120, 26, H), 0, 'a top-clipped row still anchors at its bottom');
  // Cut at the bottom ⇒ up to the visible edge, less the fixed band the fade covers.
  // 146px row, 100px of it showing: 146 - (100 - 12).
  assert.strictEqual(revealVisibleBottom(400, 546, H), 146 - (100 - REVEAL_SMOOTH_FEATHER_PX));
  // The reported bug: on a TALL bubble the old 10%-of-height band parked the pill
  // halfway up the visible slice. The band is a constant, so height cannot move it —
  // the pill's bottom edge stays 12px inside the cut, whatever the row measures.
  for (const h of [120, 400, 1200]) {
    const top = H - 60;                       // 60px of the row showing, bottom cut
    const bottomEdge = h - revealVisibleBottom(top, top + h, H);
    assert.strictEqual(bottomEdge, 60 - REVEAL_SMOOTH_FEATHER_PX, `h=${h}: a constant, not a ratio`);
  }
  // On a sliver the band is capped at half the slice — being seen beats being clear of
  // a band that is fully faded anyway (such a row hides its trigger, see below).
  assert.strictEqual(revealVisibleBottom(495, 641, H), 146 - 2.5);
});

// ── …and it is not shown at all when it cannot be placed cleanly ────────────
test('revealTriggerFits: hidden on a slice too short to hold it, always on a whole row', () => {
  const H = 500;
  const room = REVEAL_TRIGGER_SIZE + REVEAL_TRIGGER_PAD + REVEAL_SMOOTH_FEATHER_PX;   // 37
  // A row you can see in full always keeps its trigger — it sits on that row's own
  // bottom edge and touches nothing, however short the bubble is.
  assert.strictEqual(revealTriggerFits(40, 160, H), true);
  assert.strictEqual(revealTriggerFits(40, 58, H), true, 'a one-line bubble still gets one');
  // Bottom-clipped: enough of the slice to hold the pill clear of the bubble below…
  assert.strictEqual(revealTriggerFits(H - room, H - room + 400, H), true);
  // …and not a pixel less, or it would be drawn across the neighbour.
  assert.strictEqual(revealTriggerFits(H - room + 1, H - room + 401, H), false);
  // Top-clipped rows need no band, so they need less slice.
  assert.strictEqual(revealTriggerFits(-380, 20, H), false, 'a 20px sliver at the top: no room');
  assert.strictEqual(revealTriggerFits(-380, 45, H), true);
  // Hysteresis: a row already hidden needs 2px more before it comes back, so a scroll
  // grazing the threshold cannot flutter the pill on and off.
  assert.strictEqual(revealTriggerFits(H - room, H - room + 400, H, true), false);
  assert.strictEqual(revealTriggerFits(H - room - 2, H - room + 398, H, true), true);
  // Nothing on screen / an empty row: nothing to place.
  assert.strictEqual(revealTriggerFits(600, 720, H), false);
  assert.strictEqual(revealTriggerFits(10, 10, H), false);
});

test('the chat transcript takes the SMOOTH fade; other reveal targets keep the grain', () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.match(view, /observeReveal\(transcript, '\[data-row\]', \{ smooth: true \}\)/,
    'text rows opt out of the dot grain');
  const projects = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');
  assert.match(projects, /observeReveal\(list, '\.project-row'\)/, 'project rows are untouched');
  const motion = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  const apply = motion.slice(motion.indexOf('const apply = ()'), motion.indexOf('const schedule'));
  assert.match(apply, /if \(!smooth\) row\.el\.style\.setProperty\('--dissolve'/, 'no grain ramp on text');
  assert.match(apply, /smooth \? REVEAL_SMOOTH_FEATHER : REVEAL_FEATHER/, 'a fixed band, not 10%');
  // The mask itself: one linear layer, no radial grain tiles, and it still reaches the
  // chrome the row paints outside its box.
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  const smooth = css.slice(css.indexOf('.reveal-item.reveal-smooth.reveal-masked {'),
    css.indexOf('.reveal-item.reveal-entering'));
  assert.ok(!smooth.includes('radial-gradient'), 'no dot grain on text rows');
  assert.ok(!smooth.includes('mask-composite'), 'nothing to compose — one layer');
  assert.match(smooth, /mask-size: 300% 100%/);
  assert.match(smooth, /mask-clip: no-clip/);
  // …while the grainy original is still there for everything else.
  assert.match(css, /\.reveal-item\.reveal-masked \{[\s\S]*?radial-gradient/);
  // A row that cannot place its trigger hides it outright.
  const comp = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.match(comp, /\.chat-msg\.reveal-no-trigger \.chat-row-menu-btn \{ display: none; \}/);
  assert.match(apply, /classList\.toggle\(REVEAL_NO_TRIGGER_CLASS, hide\)/);
});

test('the trigger reads --visible-bottom, and the observer publishes it for EVERY row', () => {
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const btn = /\.chat-row-menu-btn \{([\s\S]*?)\n\}/.exec(css)[1];
  assert.match(btn, /bottom: calc\(var\(--visible-bottom, 0px\) \+ var\(--row-menu-lift, 0px\)\)/,
    'anchored to the visible slice, plus the jump-pill clearance lift');
  assert.match(btn, /width: 21px; height: 21px/, 'the size the geometry above assumes');
  // Hover reveals it, so a partially visible row must answer hover on its visible
  // slice — the rule hangs off the ROW, which is what the pointer is over.
  // …revealed translucent, with only the hovered trigger itself going fully opaque.
  assert.match(css, /\.chat-msg:hover \.chat-row-menu-btn \{ opacity: \.7; \}/);
  assert.match(css, /\.chat-msg:hover \.chat-row-menu-btn:hover \{ opacity: 1; \}/);
  const motion = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  const apply = motion.slice(motion.indexOf('const apply = ()'), motion.indexOf('const schedule'));
  const published = apply.indexOf("setProperty('--visible-bottom'");
  assert.ok(published > 0, 'apply() publishes it');
  assert.ok(published < apply.indexOf('if (!masked) continue;'),
    'BEFORE the masked-only bailout — an unmasked row needs it too');
  assert.ok(apply.includes('if (vb !== row.visBottom)'), 'written only when it moves');
  // One scroll listener, not two: it rides the reveal observer already bound per
  // transcript (chatView.js), so nothing else has to watch the scroller.
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.ok(view.includes("observeReveal(transcript, '[data-row]'"));
  assert.ok(!view.includes("transcript.addEventListener('scroll'"),
    'the renderer adds no scroll listener of its own');
});

test('the reveal mask reaches the chrome a row paints OUTSIDE its box', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
    css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  // The "…" trigger sits BESIDE the bubble, so it is outside the masked box. Clipping
  // the mask to that box cut it away entirely — invisible AND un-hittable, while the
  // row itself stayed clickable. Both halves of the fix, in both spellings:
  assert.strictEqual(rest.split('mask-clip: no-clip').length - 1, 2, 'the mask does not clip to the box');
  // …and the wipe layer is stretched past the box, or the area outside it would be
  // covered only by the (nearly transparent) dot grain.
  assert.strictEqual(rest.split('mask-size: 4px 4px, 7px 7px, 11px 11px, 300% 100%').length - 1, 2);
  assert.strictEqual(rest.split('mask-position: 0 0, 2px 3px, 5px 1px, center top').length - 1, 2,
    'centred horizontally, top-anchored — the `to bottom` gradient is unchanged');
});

test('the row text node is what CSS and the renderer agree on', () => {
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.match(css, /\.chat-msg-text \{ min-width: 0; \}/);
});

// ── §3.0: nothing is rendered after the reply ───────────────────────────────
test('a settled reply carries no progress line and no cancel — the turn is over', async () => {
  stubDom();
  const { renderChatLog } = await import('../js/ui/chatView.js?render-no-aux');
  const transcript = makeEl();
  const log = [
    { id: 1, role: 'user', text: 'outline everything' },
    { id: 2, role: 'assistant', text: '…', pending: true },
  ];
  renderChatLog(transcript, log, {});
  Object.assign(log[1], { pending: false, text: 'Outlined all seventeen.' });
  renderChatLog(transcript, log, {});
  const row = rowsOf(transcript)[1];
  const kids = descendants(row);
  assert.strictEqual(kids.filter((d) => d.classList.contains('chat-typing')).length, 0, 'not answering');
  for (const cls of ['chat-aux', 'chat-aux-spin', 'chat-aux-stop', 'chat-aux-text']) {
    assert.strictEqual(kids.filter((d) => d.classList.contains(cls)).length, 0, `${cls} is gone`);
  }
  assert.strictEqual(row.textContent, 'Outlined all seventeen.', 'the reply, and nothing after it');
  // Even if a stale row carried the old field, there is no renderer for it.
  log[1].aux = { phase: 'refining', done: 4, total: 17 };
  renderChatLog(transcript, log, {});
  assert.strictEqual(descendants(row).filter((d) => d.classList.contains('chat-aux')).length, 0);
  assert.strictEqual(row.textContent, 'Outlined all seventeen.');
  // The view no longer knows the concept at all.
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.ok(!/syncAuxNote|auxNoteText|onAuxCancel|chat-aux/.test(view));
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.ok(!/\.chat-aux/.test(css), 'and neither does the stylesheet');
});

// ── Arrivals are dust too (motion.js chatIn) ────────────────────────────────
// A message coming apart already had particles; a message APPEARING had nothing —
// which is what made the two directions read as different surfaces. Every entry that
// arrives now gathers out of its own dust, on the same fine mesh the removal uses.
test('an appearing entry plays the gather; a transcript’s FIRST paint does not', async () => {
  stubDom();
  globalThis.matchMedia = () => ({ matches: false });
  const { renderChatLog } = await import('../js/ui/chatView.js?render-enter');
  const { CHAT_ENTERING_CLASS } = await import('../js/ui/motion.js');
  const transcript = makeEl();
  // Opening a surface onto history it MISSED is not a conversation happening in front
  // of you — the first paint is silent, however many rows it lands.
  const log = [{ id: 1, role: 'user', text: 'hi' }, { id: 2, role: 'assistant', text: 'hello' }];
  renderChatLog(transcript, log, {});
  assert.deepStrictEqual(rowsOf(transcript).map((r) => r.classList.contains(CHAT_ENTERING_CLASS)),
    [false, false], 'history assembles silently');

  // A fresh turn: the user's bubble and the pending "…" both arrive.
  log.push({ id: 3, role: 'user', text: 'now crop it' },
           { id: 4, role: 'assistant', text: '…', pending: true });
  renderChatLog(transcript, log, {});
  const [, , userRow, pendingRow] = rowsOf(transcript);
  assert.ok(userRow.classList.contains(CHAT_ENTERING_CLASS), 'the message you sent arrives');
  // …but the "…" placeholder does NOT: it lives about as long as the gather itself, so
  // dusting it in kept it veiled for almost its whole life and the bouncing dots were
  // never seen. Its arrival is the settle below.
  assert.ok(!pendingRow.classList.contains(CHAT_ENTERING_CLASS),
    'the in-flight placeholder is not dusted in — you have to be able to see the dots');
  // …and the rows already on screen are left alone: an arrival is not a repaint.
  assert.ok(!rowsOf(transcript)[0].classList.contains(CHAT_ENTERING_CLASS));

  // The ANSWER lands in the element the dots held, so "a new node appeared" would have
  // missed it entirely — a pending row settling is an arrival in its own right.
  pendingRow.classList.remove(CHAT_ENTERING_CLASS);
  Object.assign(log[3], { pending: false, text: 'Cropped.' });
  renderChatLog(transcript, log, {});
  assert.ok(pendingRow.classList.contains(CHAT_ENTERING_CLASS), 'the reply arrives as dust');
  // A settled row that merely repaints must not replay it.
  pendingRow.classList.remove(CHAT_ENTERING_CLASS);
  renderChatLog(transcript, log, {});
  assert.ok(!pendingRow.classList.contains(CHAT_ENTERING_CLASS), 'a repaint is not an arrival');

  // A failure is a message too — same gather, whatever the row says.
  log.push({ id: 5, role: 'assistant', text: "Couldn't reach Ollama at localhost:11434 (fetch failed)",
             error: true, card: true, retryText: 'now crop it' });
  renderChatLog(transcript, log, {});
  const errRow = rowsOf(transcript).at(-1);
  assert.ok(errRow.classList.contains('chat-msg-error'));
  assert.ok(errRow.classList.contains(CHAT_ENTERING_CLASS), 'an error card arrives like any other');
  delete globalThis.matchMedia;
});

test('the arrivals share ONE mesh budget with the wipe, and run after the scroll', () => {
  // The dust is a clone per cell, so a Clear that also lands a fresh turn would put two
  // full meshes in the air at once — the count handed to chatIn is both sides together.
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.match(view, /entering\.forEach\(\(el, i\) => chatIn\(el, entering\.length \+ going\.length, i\)\)/);
  // …and it runs at the END: a cloud taken mid-build would be missing the row's own
  // text and CTAs, which are appended after the element exists.
  assert.ok(view.indexOf('entering.forEach') > view.indexOf("el.querySelector('.chat-row-menu-btn')"));
  // …AFTER the scroll pin, not before. The transcript grows by the new entry's height
  // and scrolls to it; a cloud measured first is photographed at the pre-scroll box and
  // ends up stranded above where the entry actually lands (the reported bug).
  assert.ok(view.indexOf('entering.forEach') > view.indexOf('stickToBottom(transcript)'),
    'the arrivals are armed after the transcript has been told to scroll');
  const motion = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  // An arrival is a TOAST arriving: the same speck cloud (surfaceDust — which flies on
  // <body> and paints specks, not clones carrying .chat-msg/[data-row] that everything
  // walking the transcript would read as live rows), gathered out of the row's OWN side.
  assert.match(motion, /surfaceDust\(el, chatArrivalPoint\(el\), \{ ms: CHAT_ENTER_MS, gather: true \}\)/);
  // Two frames before the measure: frame one is the new entries' layout, frame two the
  // scroll that follows it (stickToBottom pins on a rAF of its own).
  assert.match(motion, /requestAnimationFrame\(\(\) => requestAnimationFrame\(fn\)\)/);
  // …and the cloud is torn down explicitly as the veil lifts, rather than left to its own
  // grace period — the layer holds its FINISHED state (opaque, at identity), which is an
  // exact second copy sitting over the real entry.
  assert.match(motion, /unveil\(\);\s*\n\s*cancelDust\(el\);/);
});

test('chatArrivalPoint: an entry gathers out of the edge it sits against', () => {
  // The toast rule, read off geometry rather than the role class, so an attachment strip
  // or a result card follows the message it rides with.
  const scroller = { getBoundingClientRect: () => ({ left: 0, right: 400 }) };
  const row = (left, right) => ({
    parentElement: scroller,
    getBoundingClientRect: () => ({ left, right, width: right - left, top: 100, height: 40 }),
  });
  const user = chatArrivalPoint(row(180, 396));        // hugging the right edge
  const bot = chatArrivalPoint(row(4, 220));           // hugging the left edge
  assert.ok(user.x > 396, 'the user\'s own messages stream in from the right');
  assert.ok(bot.x < 4, "the assistant's from the left");
  assert.equal(user.y, 120, 'at the row\'s own height');
  // …by dockAwayPoint's reach off the row's width, so the point clears the transcript.
  assert.equal(Math.round(user.x), Math.round(288 + 216 * CHAT_ENTER_REACH));
  // A row that cannot be measured settles instead of flying at a NaN point.
  assert.equal(chatArrivalPoint({ getBoundingClientRect: () => ({ left: 0, right: 0, width: 0 }), parentElement: scroller }), null);
  assert.equal(chatArrivalPoint(null), null);
});

test('dustFitsScroller: only a whole entry inside its scroller may fly', () => {
  const at = (top, bottom) => ({ getBoundingClientRect: () => ({ top, bottom, width: 200, height: bottom - top }) });
  const scroller = at(100, 400);
  assert.ok(dustFitsScroller(at(120, 200), scroller), 'wholly inside');
  assert.ok(dustFitsScroller(at(100, 400), scroller), 'exactly filling it');
  // The cloud is position:fixed, so the transcript does NOT clip it — an entry still
  // below the fold would scatter its motes across the composer under it, which is what
  // put dust over the input box.
  assert.ok(!dustFitsScroller(at(350, 460), scroller), 'hanging past the bottom');
  assert.ok(!dustFitsScroller(at(40, 150), scroller), 'hanging past the top');
  assert.ok(!dustFitsScroller(at(0, 900), scroller), 'taller than the scroller');
  assert.ok(!dustFitsScroller(at(120, 120), scroller), 'zero height');
  assert.ok(!dustFitsScroller(null, scroller), 'no element');
  assert.ok(!dustFitsScroller(at(120, 200), null), 'no scroller');
});

test('animations.css: an arriving entry is VEILED, never faded up under its own dust', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  const rule = css.slice(css.indexOf('.chat-transcript > .chat-entering'));
  // A veil, not a keyframed fade: the entry is not seen at all until the motes land, so
  // the animation can never play over an already-visible message (the reported bug).
  assert.match(rule, /^\.chat-transcript > \.chat-entering[\s\S]{0,200}?opacity: 0 !important;/);
  assert.match(rule.slice(0, 400), /animation: none !important;/,
    'the dust is a photograph of where the entry IS — nothing may move under it');
  assert.match(rule.slice(0, 400), /transition: none !important;/);
  assert.match(css, /\.ctx-assist-transcript > \.chat-entering/, 'the flyout transcript too');
  // The old fade is gone for good — keeping it would re-introduce exactly the bug.
  assert.ok(!/chatCardEnter/.test(css), 'no fade-up keyframes survive');
  // The entry keeps its HEIGHT while veiled (opacity only, never display/height), so the
  // transcript grows and scrolls to it exactly as it always did.
  assert.ok(!/\.chat-entering[\s\S]{0,200}?(display: none|height: 0)/.test(rule.slice(0, 400)));
});

test('chatIn: veils at once, lifts only when the motes have landed', async () => {
  const { chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS, ITEM_DUST_MS } = await import('../js/ui/motion.js');
  // The gather rides the same clock the scatter falls on — the two directions are one
  // motion, so one number owns both. That clock is ITEM_DUST_MS, NOT the connections
  // list's DISINTEGRATE_MS: a message is read while it arrives, a URL row is not, and the
  // two were deliberately parted. Shorter than the leave on purpose — the motes carry no
  // text, so a long answer is unreadable until the veil lifts — and a SHARE of it rather
  // than a number of its own, so a change to one clock never has the two meet.
  assert.ok(CHAT_ENTER_MS < ITEM_DUST_MS && CHAT_ENTER_MS >= ITEM_DUST_MS / 2,
    `chat arrival ${CHAT_ENTER_MS}ms of a ${ITEM_DUST_MS}ms message flight`);
  const classes = new Set();
  const el = { classList: {
    add: (...c) => c.forEach((x) => classes.add(x)),
    remove: (...c) => c.forEach((x) => classes.delete(x)),
    contains: (c) => classes.has(c),
  } };
  globalThis.matchMedia = () => ({ matches: false });
  const p = chatIn(el);
  // SYNCHRONOUSLY veiled — before the caller returns, so no frame ever paints the entry
  // ahead of its own dust.
  assert.ok(classes.has(CHAT_ENTERING_CLASS), 'veiled from the first frame');
  await p;
  // No DOM here, so no motes can fly (disintegrate bails) — then the veil must lift at
  // once rather than hiding the entry behind a flight that never happened.
  assert.ok(!classes.has(CHAT_ENTERING_CLASS), 'never left stranded invisible');

  // Reduced motion: no veil at all — the entry is simply THERE.
  globalThis.matchMedia = () => ({ matches: true });
  await chatIn(el);
  assert.ok(!classes.has(CHAT_ENTERING_CLASS));
  // Decoration only: a missing element must never make an append throw.
  await chatIn(null);
  delete globalThis.matchMedia;
});

test('a flying cloud is re-anchored to its entry, and dropped if the entry leaves', () => {
  // The layer is position:fixed at the box measured when it launched, but a transcript
  // SCROLLS under it — the bottom-pin fires again on a 220ms timer, and a later turn
  // appends more rows. A cloud left where it started is drawn over whatever has since
  // moved into those coordinates: the reported "text appears mid-animation, breaking the
  // UI" (a user bubble's motes rendered on top of the error card below it).
  const src = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  assert.match(src, /retargetDust\(el, r\);/, 'the cloud follows its entry per frame');
  assert.ok(/dustFitsScroller\(el\)/.test(src),
    'and is dropped outright once the entry is no longer wholly in the scroller');
  // …and a subject that RESIZES mid-flight (a re-wrapped label, a font landing, the panel
  // dragged wider) leaves a cloud that no longer matches what arrives — there is no
  // re-photographing it, so the stale copy is dropped rather than shown at the wrong size.
  assert.match(src, /const resized = !r \|\| !shot/);
  assert.match(src, /if \(!dustFitsScroller\(el, el\.parentElement, r, s\) \|\| resized\) \{\s*\n\s*cancelDust\(el\); live = false; onDrop\(\); return;/);
  // Fonts first: a webfont landing after the photograph re-wraps the entry and widens it.
  assert.match(src, /globalThis\.document\?\.fonts\?\.ready/);
  // …armed for the flight and stopped with it, so nothing keeps ticking after the cut.
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  assert.match(src, /stop\(\);\s*\n\s*handOver\(\);/);
  // An element torn out mid-flight (the transcript cleared, a row replaced) measures as
  // a zero box, so the same check reaps its orphaned cloud within a frame.
  assert.match(src, /export function retargetDust\(el, r = null\) \{/);
});

test('a flying cloud is clipped to its scroller, so no mote lands on the composer', () => {
  // On the desktop the overlay is a real widget and paints only inside its own box, so a
  // mote can never reach the composer. Here the tiles translate freely out of an
  // `overflow: visible` host, and a gather next to the input rained motes across it
  // (reported). The clip is against the host's OWN border box, so the insets are signed:
  // negative EXPANDS it, letting a mote fly anywhere inside the transcript and nowhere
  // outside it.
  const src = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  assert.match(src, /const clipDustToScroller = \(el, scroller = el\?\.parentElement, r = null, s = null\) => \{/);
  assert.match(src, /host\.style\.clipPath =/);
  assert.match(src, /inset\(\$\{px\(s\.top - r\.top\)\} \$\{px\(r\.right - s\.right\)\} \$\{px\(r\.bottom - s\.bottom\)\} \$\{px\(s\.left - r\.left\)\}\)/);
  // Applied before the first painted frame, then kept in step per frame (both boxes move).
  assert.match(src, /clipDustToScroller\(el\);   \/\/ before the first frame paints, not after it/);
  assert.match(src, /retargetDust\(el, r\);\s*\n\s*clipDustToScroller\(el, el\.parentElement, r, s\);/);
});

test('dropping the cloud hands the entry over in the SAME frame', () => {
  // The veil is lifted by a timer at the end of the FULL flight. A cancel that only killed
  // the motes therefore left the message invisible, with nothing standing in for it, until
  // that timer fired — up to the whole gather. Found by resizing a live entry mid-flight.
  const src = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  assert.match(src, /const trackDust = \(el, ms, onDrop = \(\) => \{\}\) => \{/);
  assert.match(src, /cancelDust\(el\); live = false; onDrop\(\); return;/);
  assert.match(src, /const stop = trackDust\(el, CHAT_ENTER_MS, handOver\);/);
  // …and exactly once, whichever path gets there first (a drop, or the flight ending).
  assert.match(src, /if \(handedOver\) return;/);
});

// ── A wrapped bubble hugs its own longest line, not the max-width cap ───────────────
// (js/ui/chatView.js applyShrinkWrap) — reported: a two-line error card (long explanatory
// text + a much narrower "Configure provider" button) rendered at the FULL 88% cap
// instead of its own widest line, stranding the shorter line in dead space.
test('shrinkWrapWidth: one line already hugs its content — nothing to pin', () => {
  assert.strictEqual(shrinkWrapWidth([193.4]), null);
  assert.strictEqual(shrinkWrapWidth([]), null);
  assert.strictEqual(shrinkWrapWidth(null), null);
});

test('shrinkWrapWidth: two+ lines pin to the WIDEST one, rounded up', () => {
  assert.strictEqual(shrinkWrapWidth([193.4, 182.1]), 194);
  assert.strictEqual(shrinkWrapWidth([120, 300, 45]), 300);
  // A degenerate (zero-width) measurement declines rather than pinning a collapsed box.
  assert.strictEqual(shrinkWrapWidth([0, 0]), null);
});
