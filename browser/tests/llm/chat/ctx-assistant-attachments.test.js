// A turn's images belong to the user's row (js/llm/chat/session.js): the previews, what
// §12.1 persists, and the composer drop target the rows leave before the empty state returns.
import { test } from 'node:test';
import assert from 'node:assert';
import { chatDropCueHtml } from '../../../js/ui/chat/view.js';
import { wipeDurationMs, LEAVE_MS, DISINTEGRATE_MS } from '../../../js/ui/motion.js';
import { setMotionOverride } from '../../../js/ui/motion/motionPrefs.js';
import { chatLog, resetChatLog, runLoggedChatTurn, attachmentPreviews } from '../../../js/llm/chat/session.js';
import { buildChatDoc, rowsToMessages } from '../../../js/llm/chat/store.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from '../../helpers/css.js';
import { makeEl, stubDom } from '../../helpers/chatTranscriptRig.js';

// renderChatLog over the transcript rig, every setTimeout it arms held back for the test to fire.
const paintRig = async (t, tag) => {
  stubDom();
  const timers = [];
  const realSetTimeout = globalThis.setTimeout;
  globalThis.setTimeout = (fn, ms) => timers.push({ fn, ms });
  t.after(() => { globalThis.setTimeout = realSetTimeout; });
  const { renderChatLog } = await import(`../../../js/ui/chat/view.js?attach-${tag}`);
  const transcript = makeEl();
  return { transcript, timers, paint: (log) => renderChatLog(transcript, log, {}) };
};

// ── Clearing: the rows leave FIRST, the empty state comes back after ──
test('the empty state waits out the wipe instead of appearing under the falling rows', async (t) => {
  const { transcript, timers, paint } = await paintRig(t, 'empty');
  const chips = () => transcript.querySelector('.chat-empty');
  const waiters = () => timers.filter((x) => x.ms === wipeDurationMs());
  paint([{ id: 1, role: 'user', text: 'hi' }]);
  paint([]);
  // The placeholder is never painted in the same tick as the removal…
  assert.strictEqual(chips(), null, 'no chips under the falling rows');
  assert.strictEqual(waiters().length, 1, 'it waits for the wipe to finish');
  // One waiter at a time — renderChatLog runs on every log change.
  paint([]);
  assert.strictEqual(waiters().length, 1, 'a repaint mid-wipe arms no second waiter');
  waiters()[0].fn();
  assert.ok(chips(), 'the chips come back once the wipe is over');
  // …and when it runs it re-checks the world: a turn started during the wipe is never papered over.
  const again = await paintRig(t, 'restart');
  again.paint([{ id: 1, role: 'user', text: 'hi' }]);
  again.paint([]);
  again.paint([{ id: 2, role: 'user', text: 'again' }]);
  again.timers.filter((x) => x.ms === wipeDurationMs()).forEach((x) => x.fn());
  assert.strictEqual(again.transcript.querySelector('.chat-empty'), null, 'the new turn keeps the transcript');
});

test('the wipe lasts as long as the scatter, and nothing when nothing flies', () => {
  // The SCATTER, not the row collapse: leaveThenRemove resolves on LEAVE_MS while the particles keep
  // falling for DISINTEGRATE_MS — a caller on its OWN dust clock (ITEM_DUST_MS) waits out that.
  assert.strictEqual(wipeDurationMs(), Math.max(LEAVE_MS, DISINTEGRATE_MS));
  assert.strictEqual(wipeDurationMs(DISINTEGRATE_MS * 2), DISINTEGRATE_MS * 2, 'its own dust clock');
  assert.strictEqual(wipeDurationMs(1), LEAVE_MS, 'never shorter than the collapse');
  // A mode that flies NOTHING waits for nothing: the row's own collapse is the whole wipe.
  setMotionOverride({ mode: 'slide' });
  try { assert.strictEqual(wipeDurationMs(), 0, 'no particles, no wait'); } finally { setMotionOverride(null); }
});

// ── The images a turn carries belong to the USER's row ──
test('attachmentPreviews reduces the queued attachments to renderable thumbnails', () => {
  assert.deepStrictEqual(attachmentPreviews(null), []);
  assert.deepStrictEqual(attachmentPreviews({ attachments: [] }), []);
  const previews = attachmentPreviews({
    attachments: [
      { name: 'cat.jpg', kind: 'image', use: 'analyze', dataUrl: 'data:image/jpeg;base64,AAA' },
      // A video is represented by the frames actually sent to the model (§7).
      { name: 'clip.mp4', kind: 'video', frames: ['data:image/jpeg;base64,BBB', 'data:image/jpeg;base64,CCC'] },
      // Nothing renderable (a queue entry still decoding) is skipped, not shown blank.
      { name: 'pending.png', kind: 'image' },
    ],
  });
  assert.deepStrictEqual(previews, [
    { name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' },
    { name: 'clip.mp4', kind: 'video', dataUrl: 'data:image/jpeg;base64,BBB' },
  ]);
});

test('the logged turn hangs the attachments on the user row, and §12.1 still persists text only', async () => {
  resetChatLog();
  const controller = {
    attachments: [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' }],
    async send() { return { reply: 'A cat.', warnings: [], results: [] }; },
  };
  await runLoggedChatTurn(controller, 'what is this?');
  const [user, assistantRow] = chatLog();
  assert.strictEqual(user.role, 'user');
  assert.deepStrictEqual(user.attachments, [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' }]);
  assert.strictEqual(assistantRow.attachments, undefined, 'the reply is not the one that attached them');
  // The transcript may show images; the persisted document must never hold them.
  const messages = rowsToMessages(chatLog());
  assert.deepStrictEqual(messages, [{ role: 'user', text: 'what is this?' }, { role: 'assistant', text: 'A cat.' }]);
  assert.ok(buildChatDoc(messages).messages.every((m) => !('attachments' in m) && !('images' in m)));
  resetChatLog();
});

test('renderChatLog paints the attachments as the user\'s own strip, above their message', async (t) => {
  const { transcript, paint } = await paintRig(t, 'strip');
  const log = [{ id: 7, role: 'user', text: 'look', attachments: [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:,' }] },
    { id: 8, role: 'assistant', text: 'A cat.' }];
  paint(log);
  // Inserted BEFORE the message row (the images come with what was said)…
  const [strip, message] = transcript.children;
  assert.ok(strip.classList.contains('chat-attached'), 'the strip precedes the message it belongs to');
  assert.deepStrictEqual([strip.dataset.row, message.dataset.row], ['7-attachments', '7']);
  // …keyed like the result cards, so a repaint updates in place instead of reloading every thumbnail.
  log[0].text = 'look here';
  paint(log);
  assert.strictEqual(transcript.children[0], strip, 'the same strip survives the repaint');
  assert.strictEqual(transcript.querySelectorAll('.chat-attached').length, 1);
  // The strip sits on the user's side — assistant-side would read as the model's.
  const css = COMPONENTS_CSS;
  assert.match(css, /\.chat-attached \{[^}]*align-self: flex-end/);
  assert.match(css, /\.chat-attached-thumb \{[^}]*object-fit: cover/);
});

test('the drop target is the COMPOSER, cued by an animated icon over it', () => {
  const css = COMPONENTS_CSS;
  const cue = /\.chat-drop-cue \{([^}]*)\}/.exec(css);
  assert.ok(cue, 'a drag over the composer paints a labelled overlay');
  assert.match(cue[1], /position: absolute;/);
  assert.match(cue[1], /pointer-events: none;/, 'the overlay must not swallow the drop');
  // Shown only while the COMPOSER row is the drag target — not the whole panel.
  assert.match(css, /\.chat-input-wrap\.chat-drop-target \.chat-drop-cue \{ display: flex; \}/);
  assert.ok(!/stencil-chat-panel\.chat-drop-target::after/.test(css), 'the whole-panel overlay is gone');
  // Icon beside the label, and it animates (motion lives in animations.css).
  assert.ok(chatDropCueHtml().includes('chat-drop-cue-icon'));
  assert.match(chatDropCueHtml(), /Drop to attach/);
  const anims = ANIMATIONS_CSS;
  assert.match(anims, /\.chat-drop-cue-icon \{ animation: chat-drop-bob/);
  assert.match(anims, /@keyframes chat-drop-bob/);
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\n    \.chat-drop-cue-icon \{ animation: none/);
  // The drop overlay must clear the chat panel's z-index (90000), or dragging an image in with
  // the chat open shows the transcript where the drop zones belong.
  assert.match(css, /#global-drop-overlay \{[\s\S]*?z-index: 90002;/);
  assert.match(css, /chat-dock-left\) #global-drop-overlay \{ left: var\(--chat-size, 340px\); \}/);
});
