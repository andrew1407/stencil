// The panel's "Assistant" section (llm-contract.md §8): composes ./assistant/* around
// src/llm/chatController. Conversation state lives with this document only, never
// persisted. LLM output is DATA: it renders via textContent, never innerHTML.
import { getSettings } from '../lib/stencil.js';
import { bindShrinkWrapResize } from '../lib/chat/chatUi.js';
import { observeReveal } from '../lib/motion.js';
import { createJumpPills } from './assistant/jumpPills.js';
import { createAttachments } from './assistant/attachments.js';
import { createTranscript } from './assistant/transcript.js';
import { createCapabilities } from './assistant/capabilities.js';
import { createResults } from './assistant/results.js';
import { createTurnRunner } from './assistant/turnRunner.js';
import { createBoot } from './assistant/boot.js';

// A hidden section is not a collapsed one (the drag spring skips it).
export const applyAssistantVisibility = (enabled, { section, button } = {}) => {
  const on = !!enabled;
  if (section) section.hidden = !on;
  if (button) button.hidden = !on;
  return on;
};

// The injected accessors are live, per call; an absent §8 op means "not supported here".
export const createAssistant = ({ getItems, getTabId, getPageUrl, openHere = () => false,
                                 pinImage: pinEntry, unpinImage: unpinEntry,
                                 rescan: rescanPage, setTheme, setFilters, setAccent }) => {
  const sectionEl = document.getElementById('sec-assistant');
  const transcriptEl = document.getElementById('chat-transcript');
  const inputEl = document.getElementById('chat-input');
  const sendBtn = document.getElementById('chat-send');
  const trayEl = document.getElementById('chat-tray');
  const clearBtn = document.getElementById('chat-clear');
  // A bubble rendered while the section was collapsed measures zero and skips its
  // shrink-wrap pin; re-pin once the transcript has a real size.
  bindShrinkWrapResize(transcriptEl);

  // The dust layers are position:fixed clouds owning their own alpha, not rows: the
  // scroll-edge mask would sand their particles away.
  observeReveal(transcriptEl, ':scope > *:not(.disintegrate-host)');

  const { resync: resyncJumps } = createJumpPills(transcriptEl);

  // `send` and `controller` are late-bound.
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
    // A collapsed section is display:none, so the pills re-sync once it has a height.
    handleToggle(collapsed) { if (!collapsed) { boot(); resyncJumps(); } },

    // Expands through the section head so aria-expanded and the toggle hook stay in sync.
    reveal() {
      if (sectionEl.classList.contains('collapsed')) sectionEl.querySelector('.section-head').click();
      sectionEl.scrollIntoView({ block: 'end' });
      inputEl.focus();
    },
  };
};
