// A turn's images belong to the user's row (js/llm/chatSession.js): the previews, what
// §12.1 persists, and the composer drop target the rows leave before the empty state returns.
import { test } from 'node:test';
import assert from 'node:assert';
import { chatDropCueHtml } from '../js/ui/chat/chatView.js';
import { chatLog, resetChatLog, runLoggedChatTurn, attachmentPreviews } from '../js/llm/chat/chatSession.js';
import { buildChatDoc, rowsToMessages } from '../js/llm/chat/chatStore.js';
import { motionSource } from './helpers/motionSource.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';

// ── Clearing: the rows leave FIRST, the empty state comes back after ──
test('the empty state waits out the wipe instead of appearing under the falling rows', () => {
  const view = chatViewSource();
  // The placeholder is never painted in the same tick as the removal…
  assert.match(view, /const restoreEmptyState = \(transcript, log, wiped\) => \{/);
  assert.ok(view.includes('setTimeout(paint, wipeDurationMs());'), 'it waits for the wipe to finish');
  // …and when it finally runs it re-checks the world: a turn started during the wipe
  // must not be papered over with chips.
  assert.ok(view.includes("if (log.length || transcript.querySelector('[data-row]')) return;"));
  // One waiter at a time — renderChatLog runs on every log change.
  assert.ok(view.includes('if (transcript._emptyWaiting) return;'));
  // The wipe's true length is the SCATTER, not the row collapse: leaveThenRemove
  // resolves on LEAVE_MS while the particles keep falling for DISINTEGRATE_MS.
  const motion = motionSource();
  assert.match(motion, /export const wipeDurationMs = \(dustMs = 0\) => \{[\s\S]*Math\.max\(LEAVE_MS, DISINTEGRATE_MS\)/);
  // …and a caller on its OWN dust clock (a chat entry, a project row — ITEM_DUST_MS)
  // waits out THAT instead, or the placeholder lands under motes still falling.
  assert.match(motion, /if \(dustMs\) return dustEnabled\(\) \? Math\.max\(LEAVE_MS, dustMs\) : LEAVE_MS;/);
  assert.match(motion, /if \(motionReduced\(\)\) return 0;/, 'reduced motion has nothing to wait for');
  // …and neither has a mode with no particles in it: the row's own collapse IS the wipe.
  assert.match(motion, /dustEnabled\(\) \? Math\.max\(LEAVE_MS, DISINTEGRATE_MS\) : LEAVE_MS/);
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

test('renderChatLog paints the attachments as the user\'s own strip, above their message', () => {
  const view = chatViewSource();
  // Keyed like the result cards, so a repaint updates rows in place instead of
  // reloading every thumbnail…
  assert.ok(view.includes('const attachId = `${row.id}-attachments`;'));
  assert.ok(view.includes('if (!transcript.querySelector(`[data-row="${attachId}"]`)) {'));
  // …and inserted BEFORE the message row (the images come with what was said).
  assert.ok(view.includes('el.before(strip);'), 'the strip precedes the message it belongs to');
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
