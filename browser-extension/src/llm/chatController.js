// ── Chat controller: history, plan execution, auto-continuation ─────────────
// The extension chat's conversation state (contract §7 + §8): the listing
// (chatListing.js) rides as a system-prompt suffix, history replays under the §7 image
// rule, and executed plans (opExecutors.js) surface as chat cards. Every chrome/DOM
// capability is INJECTED, so `node --test` drives the controller with stubs.
import { LLM_SYSTEM_PROMPT, FORBIDDEN_OPS, parseOpPlan, continuationOnly } from './op/opPlan.js';
import { attachmentNote, buildListing, buildTabsListing } from './chatListing.js';
import { createOpExecutors } from './op/opExecutors.js';

export const HISTORY_LIMIT = 32;
// Contract §7 image downscale bound — the same long edge every rasterise path uses.
export { DEFAULT_MAX_EDGE as MAX_IMAGE_EDGE } from '../lib/image/rasterize.js';

// How many images ONE message may carry (browser chatController.js twin): images are
// re-encoded and replayed per turn (§7), and past three the queue is refused out loud.
export const MAX_ATTACHMENTS = 3;

export { LISTING_LIMIT, LISTING_NAME_CHARS, LISTING_ALT_CHARS, TABS_LIMIT, TAB_TITLE_CHARS,
         listingKind, buildListing, buildTabsListing, matchListingIndex, attachmentNote } from './chatListing.js';
export { translateOpenActions } from './openActions.js';

// §13's second enforcement tooth: a forbidden op never executes even if validation let one
// through — refused with a warning. (The first tooth is the registry test.)
export const rejectForbidden = (action, x) => {
  if (!action || !FORBIDDEN_OPS.has(action.op)) return false;
  x.warnings.push(`Refused "${action.op}" — that operation is never model-drivable`);
  return true;
};

// data:mediaType;base64,payload → { mediaType, data } (the LlmImage wire shape).
export const splitDataUrl = (u) => {
  const m = /^data:([^;,]+);base64,(.*)$/s.exec(String(u || ''));
  return m ? { mediaType: m[1], data: m[2] } : null;
};

// Image replay rule (contract §7): the current turn keeps its images; of the PRIOR turns only
// the most recent image survives. Mirror of browser/js/llm/chat/chatController.js replayMessages.
export const replayMessages = (history) => {
  const msgs = history.slice(-HISTORY_LIMIT);
  const out = [];
  let keptPrior = false;
  for (let i = msgs.length - 1; i >= 0; i--) {
    const m = msgs[i];
    if (i === msgs.length - 1 && m.images && m.images.length) {
      out.unshift({ role: m.role, text: m.text, images: m.images });
    } else if (!keptPrior && m.images && m.images.length) {
      out.unshift({ role: m.role, text: m.text, images: [m.images[m.images.length - 1]] });
      keptPrior = true;
    } else {
      out.unshift({ role: m.role, text: m.text });
    }
  }
  return out;
};

// ── The controller ──────────────────────────────────────────────────────────

export const createChatController = ({
  getClient,
  getListing,
  formatOfItem = () => '',
  focusImage,
  openImage,
  attachImage,
  pageUrl = () => '',
  getTabs = async () => [],
  scanTab,
  pinImage,
  unpinImage,
  rescan,
  openUrlImage,
  setTheme,
  setFilters,
  setAccent,
  clearChat,
} = {}) => {
  const history = [];   // [{ role, text, images? }] — canonical wire shape
  // §10 clearChat is deferred to the END of the turn: executors only raise this
  // turn-scoped flag; send() resolves it after every round has finished.
  let clearAsked = false;

  const pushHistory = (msg) => {
    // The §7 replay rule can never send an older turn's images again, so trim them here
    // rather than retain the base64 forever.
    if (msg.images && msg.images.length) {
      let keptPrior = false;
      for (let i = history.length - 1; i >= 0; i--) {
        const m = history[i];
        if (!(m.images && m.images.length)) continue;
        if (!keptPrior) { m.images = [m.images[m.images.length - 1]]; keptPrior = true; }
        else delete m.images;
      }
    }
    history.push(msg);
    if (history.length > HISTORY_LIMIT) history.splice(0, history.length - HISTORY_LIMIT);
  };

  // Verbatim prompt + the short dynamic context suffix (contract §4 allows appending).
  const buildSystem = (listing, tabs) => {
    let s = LLM_SYSTEM_PROMPT;
    const url = pageUrl();
    if (url) s += `\n\nCurrent page: ${url}`;
    s += listing.length
      ? `\n\nImages scanned from the current page (reference them by index):\n${buildListing(listing, { formatOfItem })}`
      : '\n\nNo images were found on the current page.';
    if (tabs && tabs.length) {
      s += `\n\nOther open browser tabs — scan one with {"op":"scanTab","tab":N} to switch the image listing to it:\n${buildTabsListing(tabs)}`;
    }
    return s;
  };

  const opExecutors = createOpExecutors({
    history, focusImage, openImage, attachImage, pinImage, unpinImage, openUrlImage,
    setTheme, setAccent, setFilters, scanTab, rescan, askClear: () => { clearAsked = true; },
  });

  const execute = async (plan, listing, tabs) => {
    const x = { listing, tabs, cards: [], warnings: plan.warnings.slice(), attached: [], attachedIndices: [], scannedTab: null, rescanned: null };
    for (const a of plan.actions) {
      if (rejectForbidden(a, x)) continue;
      await opExecutors[a.op]?.(a, x);
    }
    return x;
  };

  // `continued` marks the bounded auto-continuation round (contract §8: at most ONE per turn).
  // The listing is re-read each round; the tabs snapshot stays fixed (index stability).
  const round = async (client, tabs, continued, signal) => {
    const listing = getListing() || [];
    const raw = await client.chat({ system: buildSystem(listing, tabs), messages: replayMessages(history), signal });
    // Replay the RAW model text as the assistant turn so the model keeps answering
    // in pure-JSON form; the parsed `reply` is what the user sees.
    pushHistory({ role: 'assistant', text: raw });

    const plan = parseOpPlan(raw, { listingLength: listing.length, tabsLength: tabs.length });
    const { cards, warnings, attached, attachedIndices, scannedTab, rescanned } = await execute(plan, listing, tabs);

    // §11: the plan may also ASK. The card rides back with the turn; assistant.js renders it
    // and sends the answer as the next user message.
    const result = { reply: plan.reply, warnings, cards, ask: plan.ask || null, chatOnly: plan.chatOnly };
    if (attached.length) {
      // Attached images ride a user message so every provider replays them (§7).
      pushHistory({
        role: 'user',
        text: `Attached image${attachedIndices.length > 1 ? 's' : ''} ${attachedIndices.join(', ')} from the listing.`,
        images: attached,
      });
    }
    if (scannedTab) {
      // The context switch must be visible in the replayed history too — the
      // system suffix silently changes, but the model needs to know WHY.
      pushHistory({
        role: 'user',
        text: `[Scanned open tab ${scannedTab.index}${scannedTab.title ? ` ("${scannedTab.title}")` : ''} — the image listing now shows that tab's ${scannedTab.count} image${scannedTab.count === 1 ? '' : 's'}.]`,
      });
    }
    if (rescanned) {
      // Same rule as scanTab: the listing silently refreshed, so say why.
      pushHistory({
        role: 'user',
        text: `[Re-scanned the current page — the image listing now shows its ${rescanned.count} image${rescanned.count === 1 ? '' : 's'}.]`,
      });
    }
    if ((attached.length || scannedTab || rescanned) && continuationOnly(plan)) {
      // Auto-continuation: every action only GATHERED context (attach / scanTab /
      // rescan), so re-send the turn with it in place — ONCE (contract §8).
      if (!continued) result.continuation = await round(client, tabs, true, signal);
      else result.warnings.push('The model asked for more context — send another message to continue.');
    }
    return result;
  };

  const controller = {
    history,

    // Start a fresh conversation (history only — settings/scan state stay).
    clearConversation() { history.length = 0; },

    // `attachments` are images already encoded as LlmImages (`index` marks a drop matched to a
    // listing entry). Typed LlmErrors and invalid-plan errors propagate for the page to render.
    async send(text, { attachments = [], signal } = {}) {
      const client = getClient();
      // One tabs snapshot per turn (best-effort): the listing the model sees and
      // the indices a scanTab op uses must agree for the whole turn.
      let tabs = [];
      try { tabs = (await getTabs()) || []; } catch { tabs = []; }
      const images = attachments.map((a) => a && a.image).filter(Boolean);
      const note = attachmentNote(attachments);
      const msgText = String(text ?? '') + (note ? `\n\n${note}` : '');
      pushHistory(images.length ? { role: 'user', text: msgText, images } : { role: 'user', text: msgText });
      clearAsked = false;   // an aborted earlier turn must not leak its request
      const result = await round(client, tabs, false, signal);
      if (clearAsked) {
        // §10 clearChat, resolved LAST — after the plan's other actions and any
        // continuation round. A declined confirm is a note, never a failed plan.
        clearAsked = false;
        if (!clearChat) {
          result.warnings.push('Clearing the conversation is not supported here');
        } else {
          let confirmed = false;
          try { confirmed = !!(await clearChat()); } catch { confirmed = false; }
          if (confirmed) history.length = 0;   // the surface tears down its transcript
          result.clearChat = { confirmed };
        }
      }
      return result;
    },
  };
  return controller;
};
