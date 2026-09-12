// The shared chat composer, identical in the panel and the context-menu flyout. Model
// output is data: every string lands via textContent.
import MEDIA_TYPES from '../config/mediaTypes.json' with { type: 'json' };
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };
import { DOUBLE_CLICK_MS, LONG_PRESS_MS, PRESS_SLOP_PX } from './popover.js';
import { SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS, rectCenter, replayWaves, surfaceIn, surfaceOut, swapContent } from './motion.js';
import { announcePopup, openComposerMenus } from './chatRowMenu.js';
import { applyChatSide, toggleChatSide } from './chatLayoutPrefs.js';
import { icon } from './icons.js';

// Only Send stays inline; attach / clear / settings live behind the "…" trigger, in the
// DOM, so existing ids and listeners keep working. `·` in a title reads as bullets (tipContent.js).
export const { sendTitle: SEND_TITLE, sendTitlePlain: SEND_TITLE_PLAIN,
  voiceTitleListening: VOICE_TITLE_LISTENING, voiceTitlePaused: VOICE_TITLE_PAUSED } = UI_STRINGS.chat;

export const chatComposerActionsHtml = ({ prefix, actionsClass, gearClass, trailingHtml = '' }) => `<span class="${actionsClass}">
                <button id="${prefix}-send" disabled class="btn-icon ${prefix}-abtn" data-title="${SEND_TITLE}">${icon('send', { size: 14 })}</button>
                <span class="chat-more-wrap">
                    <button id="${prefix}-more-btn" class="btn-icon ${prefix}-abtn ${gearClass}" aria-haspopup="true" aria-expanded="false" aria-label="More actions">${icon('dots', { size: 14 })}<span id="${prefix}-status-dot" class="conn-status conn-status-connecting"></span></button>
                    <span class="chat-more-menu" id="${prefix}-more-menu" hidden>
                        <button id="${prefix}-voice" class="chat-more-item">${icon('mic', { size: 14 })}<span>Use voice input</span></button>
                        <button id="${prefix}-attach-btn" class="chat-more-item">${icon('image', { size: 14 })}<span>Add image</span></button>
                        <button id="${prefix}-clear" class="chat-more-item">${icon('trash', { size: 14 })}<span>Clear history</span></button>
                        <button id="${prefix}-swap-sides" class="chat-more-item" aria-label="Swap which side user and assistant messages sit on">${icon('swap', { size: 14 })}<span>Swap message sides</span></button>
                        <button id="${prefix}-settings-btn" class="chat-more-item" aria-label="Assistant settings — provider &amp; model">${icon('gear', { size: 14 })}<span>Settings</span></button>
                    </span>
                </span>
                <input type="file" id="${prefix}-attach-input" accept="${MEDIA_TYPES.accept.imageOrVideo}" multiple style="display:none;">${trailingHtml}
            </span>`;

// The "…" last opened, so a window raised from it knows which composer it belongs to.
let lastMoreBtn = null;
const shownBtn = (el) => {
  const r = el?.getBoundingClientRect?.();
  return r && r.width > 0 && r.height > 0 ? el : null;
};
// The trigger owns the flight: the gear item is hidden by then. Null ⇒ the window falls from above.
export const visibleChatMoreBtn = (doc = document) =>
  shownBtn(lastMoreBtn)
  || shownBtn(doc.getElementById('chat-more-btn'))
  || shownBtn(doc.getElementById('ctx-assist-more-btn'))
  || null;

export const wireChatMoreMenu = (prefix, doc = document, { onOpen } = {}) => {
  const btn = doc.getElementById(`${prefix}-more-btn`);
  const menu = doc.getElementById(`${prefix}-more-menu`);
  if (!btn || !menu) return;
  const dustPoint = () => rectCenter(btn);
// Both edges go through setOpen so the popup accounting matches the screen.
  const setOpen = (on) => {
// The flight plays while the menu is still up; the cloud is a copy on <body> (same
// contract as ui/dropdownMenu.js).
    const changed = on === menu.hidden;
    if (on) { menu.hidden = false; lastMoreBtn = btn; }
    if (changed) {
      if (on) surfaceIn(menu, dustPoint(), { ms: SURFACE_MENU_IN_MS });
      else surfaceOut(menu, dustPoint(), { ms: SURFACE_MENU_OUT_MS });
    }
    if (!on) menu.hidden = true;
    btn.setAttribute('aria-expanded', String(on));
    if (on) openComposerMenus.add(menu); else openComposerMenus.delete(menu);
    announcePopup();
  };
  const close = () => setOpen(false);
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    if (menu.hidden) onOpen?.();
    setOpen(menu.hidden);
  });
  for (const item of menu.querySelectorAll('.chat-more-item')) item.addEventListener('click', close);
  doc.addEventListener('pointerdown', (e) => { if (!menu.hidden && !menu.contains(e.target) && e.target !== btn) close(); });
  doc.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !menu.hidden) close(); });
};

// Both surfaces share the one preference (chatLayoutPrefs.js).
export const wireChatSideToggle = (prefix, transcript, doc = document) => {
  if (!transcript) return;
  applyChatSide(transcript);
  doc.getElementById(`${prefix}-swap-sides`)
    ?.addEventListener('click', () => applyChatSide(transcript, toggleChatSide()));
};

// Three faces: Stop while a turn is in flight, the mic in voice mode, otherwise Send. With
// voice available the empty-box Send is only look-disabled (aria-disabled): a disabled
// button hears no double-click / hold, which is how an empty composer switches to dictation.
export const syncComposerControls = ({ sendBtn, attachBtn, input }, sending,
  { attachFull = false, voice = null, voiceSupported = false } = {}) => {
  const hasText = !!input.value.trim();
  const mic = !sending && !!voice?.on;
  const idle = !sending && !mic && !hasText;
  let face;
  if (sending) {
    face = 'stop';
    sendBtn.disabled = false;
    sendBtn.dataset.title = 'Stop the response';
  } else if (mic) {
    face = 'mic';
    sendBtn.disabled = false;
    sendBtn.dataset.title = voice.listening ? VOICE_TITLE_LISTENING : VOICE_TITLE_PAUSED;
  } else {
    face = 'send';
    sendBtn.disabled = voiceSupported ? false : !hasText;
    sendBtn.dataset.title = voiceSupported ? SEND_TITLE : SEND_TITLE_PLAIN;
  }
// Same in-place swap as the Draw toggles (motion.js); an unchanged face is a no-op.
  swapContent(sendBtn, icon(face, { size: face === 'mic' ? 18 : 14 }), { key: face });
  sendBtn.classList.toggle('chat-send-idle', idle && voiceSupported);
  sendBtn.setAttribute('aria-disabled', String(idle));
  const listening = mic && !!voice.listening;
  const wasListening = sendBtn.classList.contains('chat-voice-listening');
  sendBtn.classList.toggle('chat-voice-on', mic);
  sendBtn.classList.toggle('chat-voice-listening', listening);
  if (listening !== wasListening) replayWaves(sendBtn, listening);
  attachBtn.disabled = sending || attachFull;
};

// A plain click acts one double-click interval later; a double-click or a hold (any
// pointer) switches between typing and dictation. Timers injected for tests.
export const createSendGesture = ({
  onClick, onSwitch, delay = DOUBLE_CLICK_MS, holdMs = LONG_PRESS_MS, slop = PRESS_SLOP_PX,
  setTimer = (fn, ms) => setTimeout(fn, ms), clearTimer = (id) => clearTimeout(id),
} = {}) => {
  let clickTimer = null;
  let holdTimer = null;
  let press = null;
  let swallow = false;
  const cancelClick = () => { if (clickTimer !== null) { clearTimer(clickTimer); clickTimer = null; } };
  const cancelHold = () => { if (holdTimer !== null) { clearTimer(holdTimer); holdTimer = null; } };
  return {
    click() {
      cancelClick();
      if (swallow) { swallow = false; return; }
      clickTimer = setTimer(() => { clickTimer = null; onClick(); }, delay);
    },
    dblclick() { cancelClick(); cancelHold(); onSwitch(); },
    pressStart({ x = 0, y = 0 } = {}) {
      swallow = false;
      press = { x, y };
      cancelHold();
      holdTimer = setTimer(() => { holdTimer = null; swallow = true; cancelClick(); onSwitch(); }, holdMs);
    },
    pressMove({ x = 0, y = 0 } = {}) {
      if (press && (Math.abs(x - press.x) > slop || Math.abs(y - press.y) > slop)) cancelHold();
    },
    pressEnd() { cancelHold(); press = null; },
  };
};

// Enter sends / Shift+Enter newline, send doubles as Stop, attach drives the hidden file
// input. `voice` = { isOn, isListening, toggleMode, toggleListening } from wireComposerVoice.
// Returns `send` so the voice wiring submits through the same path.
export const wireChatComposer = ({ input, sendBtn, attachBtn, attachInput }, { isSending, abort, submit, attachFiles, onInput, voice = null }) => {
  const send = () => {
    const text = input.value.trim();
    if (!text || isSending()) return;
    input.value = '';
    submit(text);
  };
  input.addEventListener('input', () => onInput?.());
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); return; }
    if (e.key === 'Escape' && voice?.isListening()) { e.preventDefault(); e.stopPropagation(); voice.toggleListening(); }
  });
  const gesture = createSendGesture({
    onClick: () => { if (isSending()) abort(); else if (voice?.isOn()) voice.toggleListening(); else send(); },
    onSwitch: () => voice?.toggleMode(),
  });
  sendBtn.addEventListener('click', () => gesture.click());
  sendBtn.addEventListener('dblclick', (e) => { e.preventDefault(); gesture.dblclick(); });
  sendBtn.addEventListener('pointerdown', (e) => { if (voice) gesture.pressStart({ x: e.clientX, y: e.clientY }); });
  sendBtn.addEventListener('pointermove', (e) => gesture.pressMove({ x: e.clientX, y: e.clientY }));
  sendBtn.addEventListener('pointerup', () => gesture.pressEnd());
  sendBtn.addEventListener('pointercancel', () => gesture.pressEnd());
  sendBtn.addEventListener('contextmenu', (e) => { if (voice) e.preventDefault(); });
  attachBtn.addEventListener('click', () => attachInput.click());
  attachInput.addEventListener('change', async (e) => {
    const files = [...(e.target.files || [])];
    e.target.value = '';
    await attachFiles(files);
  });
  return send;
};
