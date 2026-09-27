// An arriving entry's gather (js/ui/motion.js chatIn): the first paint plays nothing, the
// arrivals share one mesh budget with the wipe, and only a whole entry may fly.
import { test } from 'node:test';
import assert from 'node:assert';
import { dustFitsScroller, chatArrivalPoint, CHAT_ENTER_REACH } from '../../../js/ui/motion.js';
import { chatViewSource } from '../../helpers/chatViewSource.js';
import { makeEl, stubDom, rowsOf } from '../../helpers/chatTranscriptRig.js';
import { installDustPage, rect, cloudAim } from '../../helpers/dustPageRig.js';

// Every arriving entry gathers out of its own dust on the same fine mesh the removal uses,
// so the two directions read as one surface (motion.js chatIn).
test('an appearing entry plays the gather; a transcript’s FIRST paint does not', async () => {
  stubDom();
  globalThis.matchMedia = () => ({ matches: false });
  const { renderChatLog } = await import('../../../js/ui/chat/view.js?render-enter');
  const { CHAT_ENTERING_CLASS } = await import('../../../js/ui/motion.js');
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
  // The "…" placeholder is NOT dusted in: it lives about as long as the gather, so a veil
  // would hide the bouncing dots for almost its whole life.
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
  // Kept on the source: chatIn reads `count` only through scatterGridFor(count, index).cols,
  // which is zero by index alone, and it photographs two frames later whatever the call order.
  const view = chatViewSource();
  assert.match(view, /entering\.forEach\(\(el, i\) => chatIn\(el, entering\.length \+ going\.length, i\)\)/);
  // …at the END: a cloud taken mid-build would be missing the row's own text and CTAs.
  assert.ok(view.indexOf('entering.forEach') > view.indexOf("el.querySelector('.chat-row-menu-btn')"));
  assert.ok(view.indexOf('entering.forEach') > view.indexOf('stickToBottom(transcript)'),
    'the arrivals are armed after the transcript has been told to scroll');
});

test('an arrival gathers as specks on <body>, two frames after it is appended', async (t) => {
  const page = installDustPage(t);
  const { chatIn, CHAT_ENTER_MS, CHAT_ENTERING_CLASS, SCATTER_MAX_ROWS } = await import('../../../js/ui/motion.js');
  const box = rect(180, 300, 216, 40);
  const el = page.entry(() => box, rect(0, 100, 400, 400));
  chatIn(el);
  // Two frames before the measure: the new entries' layout, then the scroll that follows it.
  page.frame();
  assert.strictEqual(page.clouds().length, 0, 'nothing is photographed on the first frame');
  page.frame();
  // A toast's speck cloud, not clones carrying .chat-msg/[data-row] a transcript walk would read as rows.
  const [host] = page.clouds();
  assert.deepStrictEqual([host.__cloud.flight, host.__cloud.span], ['surfaceGather', CHAT_ENTER_MS]);
  assert.ok(host.classList.contains('dust-forming') && host.children.every((c) => c.dataset?.row === undefined));
  const aim = cloudAim(host);
  const point = chatArrivalPoint(el);
  assert.ok(Math.hypot(aim.x - point.x, aim.y - point.y) < 20, 'out of the edge the entry sits against');
  // The cloud is torn down as the veil lifts: its finished state is a second copy over the entry.
  page.fire(CHAT_ENTER_MS);
  assert.deepStrictEqual([el.classList.contains(CHAT_ENTERING_CLASS), page.clouds().length], [false, 0]);
  // Past the budget's rows an arrival only fades: no cloud, and the veil lifts at once.
  const late = page.entry(() => box, rect(0, 100, 400, 400));
  chatIn(late, SCATTER_MAX_ROWS + 1, SCATTER_MAX_ROWS);
  page.frame(); page.frame();
  assert.deepStrictEqual([page.clouds().length, late.classList.contains(CHAT_ENTERING_CLASS)], [0, false]);
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
  // The cloud is position:fixed, so the transcript does not clip it — an entry below the fold
  // would scatter its motes across the composer.
  assert.ok(!dustFitsScroller(at(350, 460), scroller), 'hanging past the bottom');
  assert.ok(!dustFitsScroller(at(40, 150), scroller), 'hanging past the top');
  assert.ok(!dustFitsScroller(at(0, 900), scroller), 'taller than the scroller');
  assert.ok(!dustFitsScroller(at(120, 120), scroller), 'zero height');
  assert.ok(!dustFitsScroller(null, scroller), 'no element');
  assert.ok(!dustFitsScroller(at(120, 200), null), 'no scroller');
});
