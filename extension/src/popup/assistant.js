// ── Embedded AI assistant section (llm-contract.md §8) ─────────────────
// The collapsed-by-default "Assistant" section of the popup / side panel / DevTools
// panel (all driven by popup.js), chatting about the surface's LIVE scan items.
// Boots lazily; conversation state lives with this document only — never persisted.
// src/llm/chatController owns history/listing/plan execution; ./assistant/ wires the
// chrome capabilities (focus highlight, `#stencil=` hand-off, authed fetch + downscale).
// LLM output is DATA: everything the model produces renders via textContent, never innerHTML.
//
// This file composes the collaborators and owns nothing else: `state` is the handful of
// values they share (busy, the controller, the working scan), and each factory takes the
// ones it needs. The order below is a dependency order, not a preference.
import { getSettings } from '../lib/stencil.js';
import { bindShrinkWrapResize } from '../lib/chatUi.js';
import { observeReveal } from '../lib/motion.js';
import { createJumpPills } from './assistant/jumpPills.js';
import { createAttachments } from './assistant/attachments.js';
import { createTranscript } from './assistant/transcript.js';
import { createCapabilities } from './assistant/capabilities.js';
import { createResults } from './assistant/results.js';
import { createTurnRunner } from './assistant/turnRunner.js';
import { createBoot } from './assistant/boot.js';

// Options needs no reload. A hidden section is not a collapsed one (drag spring skips it).
export const applyAssistantVisibility = (enabled, { section, button } = {}) => {
  const on = !!enabled;
  if (section) section.hidden = !on;
  if (button) button.hidden = !on;
  return on;
};

// Build the assistant for this surface. The injected accessors are live, per call:
// getItems/getTabId/getPageUrl expose popup.js's current scan (the §8 listing source);
// openHere imports into the editor tab the panel stands on (false = classic new-tab
// hand-off stays in charge); pinImage/unpinImage/rescan/setTheme/setFilters/setAccent
// wire §8 ops to the host's own controls (absent = "not supported here").
// Returns { handleToggle(collapsed), reveal() } — boot the chat UI on first expansion,
// and the ✦ header button's expand + scroll + focus.
export const createAssistant = ({ getItems, getTabId, getPageUrl, openHere = () => false,
                                 pinImage: pinEntry, unpinImage: unpinEntry,
                                 rescan: rescanPage, setTheme, setFilters, setAccent }) => {
  const sectionEl = document.getElementById('sec-assistant');
  const transcriptEl = document.getElementById('chat-transcript');
  const inputEl = document.getElementById('chat-input');
  const sendBtn = document.getElementById('chat-send');
  const trayEl = document.getElementById('chat-tray');
  const clearBtn = document.getElementById('chat-clear');
  // A bubble rendered while the (collapsed-by-default) section was closed measures
  // zero rects and skips its shrink-wrap pin — catch it (and any later resize) the
  // moment the transcript itself gains a real size.
  bindShrinkWrapResize(transcriptEl);

  // Transcript entries fade + lift in as they arrive and dissolve at the top edge
  // as the conversation scrolls past them (browser parity, css/animations/reveal.css). The
  // dust layers are excluded: they are position:fixed clouds owning their own alpha,
  // not rows, and the scroll curve masking them sanded the particles away (the browser
  // twin selects '[data-row]', which never matched them either).
  observeReveal(transcriptEl, ':scope > *:not(.disintegrate-host)');

  // Jump pills over the transcript's bottom edge — ./assistant/jumpPills.js.
  const { resync: resyncJumps } = createJumpPills(transcriptEl);

  // The handful of values the collaborators share. `send` and `controller` are late-bound
  // (the ask card starts the next turn; the controller is built from the capabilities).
  const state = {
    booted: false,
    busy: false,
    turnAbort: null,
    llmSettings: null,
    probeGen: 0,            // drops stale async provider probes
    settingsPromise: null,
    workingScan: null,      // a scanTab op's other-tab images, or null for our own scan
    wipeAfterTurn: false,   // §10 clearChat, confirmed mid-turn
    controller: null,
    send: () => {},
    addMsgMenuBtn: () => {},   // wired at boot, once the message menu exists
  };

  // General (non-LLM) extension settings, read repeatedly by focus/open — cached
  // and invalidated when they change (they live in chrome.storage.sync).
  const cachedSettings = () => (state.settingsPromise ||= getSettings());

  const ops = createCapabilities({ getItems, getTabId, getPageUrl, openHere, pinEntry,
                                   unpinEntry, rescanPage, setTheme, setFilters, setAccent,
                                   cachedSettings, state });
  const tray = createAttachments({ trayEl, transcriptEl, clearBtn, getItems, getPageUrl,
                                   addWarn: (text) => view.addWarn(text),
                                   attachImage: ops.attachImage, isBusy: () => state.busy });
  const view = createTranscript({ sectionEl, transcriptEl, inputEl, tray, state });
  const { renderResult } = createResults({ view, getItems, state });
  const { send } = createTurnRunner({ sendBtn, inputEl, view, tray, renderResult, state });
  state.send = send;
  const boot = createBoot({ sectionEl, transcriptEl, inputEl, sendBtn, clearBtn,
                            view, tray, send, state });

  return {
    // Wired into popup.js's generic section toggler for #sec-assistant. A collapsed
    // section is display:none, so the transcript measures zero — re-sync the pills
    // once expanding gives it a real height.
    handleToggle(collapsed) { if (!collapsed) { boot(); resyncJumps(); } },

    // The ✦ header button: expand (through the section head, so aria-expanded and
    // the toggle hook stay in sync), scroll into view, focus the input.
    reveal() {
      if (sectionEl.classList.contains('collapsed')) sectionEl.querySelector('.section-head').click();
      sectionEl.scrollIntoView({ block: 'end' });
      inputEl.focus();
    },
  };
};
