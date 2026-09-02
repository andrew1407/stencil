// The assistant section is one controller (popup/assistant.js) driving THREE host
// pages — the popup, the side panel and the DevTools panel. They each carry their own
// copy of the composer markup, so an id added to one and forgotten in the others makes
// the controller's getElementById return null and its boot throw on the first
// `.innerHTML` — taking the whole section down on that surface, silently, in a way no
// unit test of the controller can see. Assert the three stay in lockstep.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const HOSTS = {
  popup: 'src/popup/popup.html',
  sidepanel: 'src/sidepanel/sidepanel.html',
  devtools: 'src/devtools/panel.html',
};

// Every id popup/assistant.js resolves on its host page. Keep this list in step with
// the controller's getElementById calls.
const REQUIRED_IDS = [
  'chat-transcript', 'chat-tray', 'chat-input', 'chat-send',
  'chat-more-btn', 'chat-more-menu', 'chat-status-dot',
  'chat-attach-btn', 'chat-attach-input', 'chat-clear', 'chat-open-options',
  'chat-jumps', 'chat-jump-top', 'chat-jump-bottom', 'chat-swap-sides',
];

const html = Object.fromEntries(
  Object.entries(HOSTS).map(([name, path]) => [name, readFileSync(new URL('../' + path, import.meta.url), 'utf8')]));

for (const [name, path] of Object.entries(HOSTS)) {
  test(`${name} (${path}) carries every id the assistant controller resolves`, () => {
    for (const id of REQUIRED_IDS) {
      assert.ok(html[name].includes(`id="${id}"`),
        `${path} is missing id="${id}" — the assistant section will throw on boot there`);
    }
  });
}

test('the composer keeps attach / clear / settings behind the … on every host', () => {
  for (const [name, path] of Object.entries(HOSTS)) {
    const menu = html[name].slice(html[name].indexOf('id="chat-more-menu"'));
    const body = menu.slice(0, menu.indexOf('</span>'));
    for (const id of ['chat-attach-btn', 'chat-clear', 'chat-open-options']) {
      assert.ok(body.includes(`id="${id}"`), `${path}: ${id} should live inside the … menu`);
    }
    // The menu ships closed, or it would cover the transcript on open.
    assert.match(menu.slice(0, 120), /hidden/, `${path}: the … menu should start hidden`);
  }
});

// Attachments belong to the USER's turn. They used to be rendered as assistant-side
// "Attached x.jpg" result cards: no image at all, and worded as if the model had said
// it. They are now a thumbnail strip on the user's side (browser chatView.js parity).
test('user attachments render as thumbnails on the user side, not as assistant cards', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /if \(attachments\.length\) addAttachments\(attachments\);/,
    'the send loop paints the strip for what the user attached');
  assert.match(src, /strip\.className = 'chat-attached';/);
  assert.match(src, /img\.className = 'chat-attached-thumb';/);
  assert.ok(!/addCard\('sparkle', Number\.isInteger\(p\.index\)/.test(src),
    'the assistant-side "Attached …" card is gone');
  // The filename is scanned-page data — it may only ride alt/title, never markup.
  assert.match(src, /img\.alt = p\.name;/);
  // Every host loads popup.css, so one rule covers popup / side panel / DevTools.
  const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');
  assert.match(css, /#sec-assistant \.chat-attached \{[^}]*align-self: flex-end/);
  assert.match(css, /#sec-assistant \.chat-attached-thumb \{[^}]*object-fit: cover/);
});

// A successful turn's side-notes (plan warnings) are NOT errors: they render as the
// dismissible yellow .warn note, never through the red .msg.error bubble — the red
// style stays reserved for actually failed turns (browser/desktop parity).
test('plan warnings render as neutral notes, never in the error style', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /for \(const w of result\.warnings\) addWarn\(w\);/,
    'renderResult routes warnings through addWarn');
  assert.ok(!/addMsg\('error', w\)/.test(src) && !/addMsg\(`msg error`/.test(src),
    'warnings never go through the error bubble');
  const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');
  const warn = /#sec-assistant \.warn \{([^}]*)\}/.exec(css);
  assert.ok(warn, 'the .warn note has its own rule');
  assert.match(warn[1], /var\(--yellow\)/, 'a warning is toned yellow…');
  assert.ok(!warn[1].includes('var(--danger)'), '…not the danger red');
});

// The popup is a fixed-height column and the Assistant is the tallest thing in it:
// unbounded, it squeezed the found-resources list down to a single clipped row with
// no scrollbar anywhere to recover it.
test('the popup column keeps the resource list alive when the Assistant expands', () => {
  const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');
  assert.match(css, /\.list \{ flex: 1 1 auto; min-height: 120px; \}/,
    'the list is content-sized so it ABSORBS the shrink (flex: 1 alone cannot), down to a floor');
  assert.match(css, /\.filters \{ flex: 0 0 auto; \}/,
    'the filter block keeps its natural height — shrinking it cut its chip rows in half');
  assert.match(css, /overflow-y: auto;\n\}/,
    'and the whole popup column scrolls as a last resort instead of clipping controls');
  assert.match(css, /#sec-assistant \{ flex: 0 0 auto; min-height: 0; max-height: min\(360px, 60vh\); \}/,
    'the Assistant is capped, and never squeezed — its composer must stay on screen');
  assert.match(css, /\.chat-transcript \{[^}]*flex: 1 1 auto; min-height: 56px;/,
    'inside it, the transcript is what gives up height — it scrolls');
});

// Dragging an image onto the Assistant used to tint the transcript BEHIND its own
// messages and chips — the drop area read as sitting under the conversation, and it
// claimed the whole section when only the composer can take a drop. The cue is now an
// animated icon + label drawn over the COMPOSER (browser .chat-drop-cue parity).
test('the drop target is the composer, cued by an animated icon over it', () => {
  const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');
  const cue = /#sec-assistant \.chat-drop-cue \{([^}]*)\}/.exec(css);
  assert.ok(cue, 'the drop-over state paints an overlay in the composer');
  assert.match(cue[1], /position: absolute;/);
  assert.match(cue[1], /z-index: 5;/);
  // The overlay must never swallow the drag events the composer itself handles.
  assert.match(cue[1], /pointer-events: none;/);
  // Theme-correct: mixed into the panel colour, never a flat white/black block.
  assert.match(cue[1], /color-mix\(in srgb, var\(--accent[^)]*\) 16%, var\(--panel\)\)/);
  assert.match(css, /#sec-assistant \.chat-composer\.drop-over \.chat-drop-cue \{ display: flex; \}/);
  // Neither the old under-the-content treatment nor the whole-section overlay remains
  // (the list's drag-to-pin cue keeps its outline+tint — there the rows ARE the target).
  assert.ok(!/#sec-assistant\.drop-over/.test(css));
  assert.match(css, /^\.list\.drag-over \{/m, 'the list keeps its own outline+tint cue');
  // The composer — not the section — is what the drop is wired to.
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /const composerEl = sectionEl\.querySelector\('\.chat-composer'\);/);
  assert.match(src, /wireDropTarget\(composerEl, \{\n\s*highlight: composerEl,/);
  assert.match(src, /cue\.className = 'chat-drop-cue';/);
  assert.match(src, /cueIcon\.className = 'chat-drop-cue-icon';/);
  // …and the icon animates (motion lives in lib/animations.css, reduced-motion safe).
  const anims = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  assert.match(anims, /\.chat-drop-cue-icon \{ animation: chat-drop-bob/);
  assert.match(anims, /@keyframes chat-drop-bob/);
});

// A 28–56px thumbnail can't tell two screenshots apart.
test('hovering a small attachment thumbnail shows it large', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /wireThumbPreview\(img, \{ caption \}\);/, 'the transcript strip is wired');
  assert.match(src, /wireThumbPreview\(img, \{ caption: p\.name \}\);/, 'and so are the pending chips');
  const ui = readFileSync(new URL('../src/lib/chatUi.js', import.meta.url), 'utf8');
  assert.match(ui, /export const wireThumbPreview = \(img, \{ doc = globalThis\.document/);
  assert.match(ui, /cap\.textContent = caption;/, 'a scanned filename stays data, never markup');
  assert.match(ui, /doc\.body\.appendChild\(box\);/, 'on the body — the popup clips its regions');
  const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');
  assert.match(css, /\.chat-thumb-preview \{[^}]*position: fixed;/);
  assert.match(css, /\.chat-thumb-preview \{[^}]*pointer-events: none;/);
  // A GLANCE, not a lightbox — clamped so the surface underneath stays readable.
  assert.match(css, /\.chat-thumb-preview img \{[^}]*max-width: 220px;[^}]*max-height: 220px;/);
});

// Browser parity (chatController.js onAttachmentsChanged): a send must not leave the
// queued chips behind. Here the queue and the chips live in one place, so the drain
// and the tray repaint must stay adjacent in send().
test('send drains the pending attachments and clears their chips at once', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /const attachments = pending\.splice\(0\);\n\s*renderTray\(\);/,
    'the tray repaint rides the drain — chips must not survive the send');
});

// Clear used to scatter the entries and prepend the empty state in the SAME tick, so
// the suggestion chips appeared under particles that were still falling.
test('clearing waits out the wipe before the empty state returns', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /}, wipeDurationMs\(\)\);/, 'the empty state is deferred by the wipe length');
  assert.match(src, /if \(busy \|\| transcriptEl\.querySelector\(':scope > \.msg, :scope > \.card, :scope > \.warn'\)\) return;/,
    'a turn started during the wipe must not be papered over with chips — and the '
    + 'check is :scope-d, or the scatter\'s own .msg clones read as live conversation');
  const motion = readFileSync(new URL('../src/lib/motion.js', import.meta.url), 'utf8');
  assert.match(src, /observeReveal, leaveThenRemove, wipeDurationMs, CHAT_LEAVE_MS, scatterGridFor,/);
  // Chat entries leave on the slower, finer dissolve — more particles, more time.
  assert.match(src, /const chatLeave = \(el, done, count = 1, index = 0\) =>\n\s*leaveThenRemove\(el, done, \{ ms: CHAT_LEAVE_MS, \.\.\.scatterGridFor\(count, index\) \}\);/);
  assert.match(motion, /export const CHAT_LEAVE_MS = 260;/);
  assert.match(motion, /export const CHAT_DISINTEGRATE_COLS = 32;/);
  // A wipe scatters every entry at once, so the mesh is budgeted by how many are going.
  assert.match(src, /going\.forEach\(\(el, i\) => chatLeave\(el, \(\) => el\.remove\(\), going\.length, i\)\);/);
  assert.match(motion, /export const scatterGridFor = \(count, index = 0\) => \{/);
  assert.match(motion, /export const SCATTER_MAX_ROWS = 12;/);
  // Opacity + transform only — an animated blur was the jank in the removal.
  const leaveCss = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  assert.ok(!/chatCardLeave[\s\S]*?filter: blur/.test(leaveCss), 'no blur in the leave keyframes');
  const anims = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  assert.match(anims, /@keyframes chatCardLeave/);
  // The shared helper waits for the SCATTER, not the row collapse.
  assert.match(motion, /export const wipeDurationMs = \(\) => \{[\s\S]*Math\.max\(LEAVE_MS, DISINTEGRATE_MS\)/);
});

// …and the mirror: an entry APPEARING had no particles at all, so the two directions
// read as different surfaces. Every append now plays the gather (motion.js chatIn).
test('every appended entry arrives as dust — armed only after the scroll', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  const motion = readFileSync(new URL('../src/lib/motion.js', import.meta.url), 'utf8');
  assert.match(motion, /export const CHAT_ENTER_MS = DISINTEGRATE_MS;/);
  assert.match(motion, /export function chatIn\(el, count = 1, index = 0, \{ host = null \} = \{\}\) \{/);
  // Every route into the transcript — messages / notes / warnings (appendDiv), the
  // attachment strip, a result-or-failure card, the §11 ask card — funnels through
  // the single appendEntry ritual, whose one chatEnter covers them all.
  assert.strictEqual(src.split('chatEnter(').length - 1, 1,
    'every transcript append rides appendEntry — none is left silent');
  for (const fn of ['appendDiv', 'addAttachments', 'addCard', 'renderAsk']) {
    const body = src.slice(src.indexOf(`const ${fn}`));
    assert.ok(body.slice(0, body.indexOf('\n  };')).includes('appendEntry('),
      `${fn} must append through appendEntry`);
  }
  // …except the in-flight "…" placeholder, which opts OUT (browser chatView.js parity):
  // it lives about as long as the gather itself, so dusting it in kept it veiled for
  // almost its whole life and the bouncing dots were never seen.
  assert.match(src, /const appendDiv = \(className, text, \{ arrive = true \} = \{\}\) => \{/);
  assert.match(src, /appendDiv\('msg assistant typing-row', '', \{ arrive: false \}\)/);
  // A failed turn's bubble STAYS on Retry, as it does in the browser and on the desktop —
  // a retry is another attempt, not an undo. Deleting it also killed it mid-flight.
  assert.match(src, /retry\.addEventListener\('click', \(\) => \{ if \(!busy\) send\(text, attachments\); \}\);/);
  assert.ok(!/el\.remove\(\); send\(text, attachments\)/.test(src), 'no bare delete on retry');
  // …and it is armed AFTER scrollDown(), never before. chatIn veils the entry
  // synchronously (it keeps its height, so the transcript grows and scrolls to it as
  // usual) then photographs it two frames later — a cloud measured before that scroll is
  // stranded above the entry, or drawn over the composer.
  for (const m of src.matchAll(/chatEnter\((\w+), sectionEl\)/g)) {
    // `scrollDown();` immediately before, give or take appendEntry's `if (arrive)` guard.
    const before = src.slice(Math.max(0, m.index - 120), m.index);
    assert.match(before, /scrollDown\(\);\s*(if \(arrive\) )?$/,
      `chatEnter(${m[1]}) must follow scrollDown(), not precede it`);
  }
  // The dust layers are position:fixed clouds owning their own alpha, not rows — the
  // scroll curve masking them sanded the particles away.
  assert.match(src, /observeReveal\(transcriptEl, ':scope > \*:not\(\.disintegrate-host\)'\);/);
});
// The … trigger's glyph is INSERTED, never assigned: the button already holds
// #chat-status-dot (the reachability badge), and innerHTML= deleted it, so the dot
// silently vanished and the rich provider tooltip lost its badge.
test('the … trigger keeps its status dot when the glyph is painted in', () => {
  const src = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
  assert.match(src, /moreTrigger\.insertAdjacentHTML\('afterbegin', icon\('dots'/,
    'the dots glyph is inserted alongside the dot, not assigned over it');
  assert.ok(!/moreTrigger\.innerHTML\s*=/.test(src),
    'assigning innerHTML on the … trigger wipes #chat-status-dot');
});
