// ── Chat controller: listing, history, plan execution, auto-continuation ────
// Owns the extension chat's conversation state (contract §7 + §8): the scanned-image
// listing rides as a system-prompt suffix, history replays in full (bounded to 32
// messages) under the §7 image replay rule, and executed plans surface as chat cards.
// Every chrome/DOM capability is INJECTED so `node --test` drives the controller with
// stubs (assistant.js wires the real ones).
import { LLM_SYSTEM_PROMPT, FORBIDDEN_OPS, parseOpPlan, continuationOnly } from './opPlan.js';
import { sameSource } from '../lib/dropEntry.js';
import { sourceOf } from '../lib/imageModel.js';

export const HISTORY_LIMIT = 32;
// Contract §7 image downscale bound — the same long edge every rasterise path uses.
export { DEFAULT_MAX_EDGE as MAX_IMAGE_EDGE } from '../lib/rasterize.js';

// How many images ONE message may carry (browser chatController.js twin): a turn's
// images are re-encoded and replayed per turn (§7), and three is already more than a
// question needs — past that the queue is refused out loud, never silently trimmed.
export const MAX_ATTACHMENTS = 3;
// Context-listing bounds (contract §8): ≤ 100 entries, names/alt text truncated.
export const LISTING_LIMIT = 100;
export const LISTING_NAME_CHARS = 48;
export const LISTING_ALT_CHARS = 64;
// Open-tabs listing bounds (contract §8): ≤ 20 entries, titles/URLs truncated.
export const TABS_LIMIT = 20;
export const TAB_TITLE_CHARS = 64;
export const TAB_URL_CHARS = 80;

// §13's second enforcement tooth: a forbidden op never executes, even if a registry
// mistake ever let one through validation — refused with a warning, action skipped.
// (The first tooth is the registry test; the parser also drops these as unknown ops.)
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

// Image replay rule (contract §7): the current turn keeps its images; of the PRIOR
// turns only the single most recent image survives; older turns replay text-only.
// (Mirror of browser/js/llm/chatController.js replayMessages.)
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

// ── Context listing (contract §8) ───────────────────────────────────────────

const truncate = (s, max) => {
  const v = String(s || '');
  return v.length > max ? v.slice(0, max - 1) + '…' : v;
};

// §8 listing kind for a scanned record (imageScan.js shape: kind 'img'|'bg'|'video'
// plus the meta/poster flags): img | background | poster | video | icon.
export const listingKind = (item) => {
  if (!item) return 'img';
  if (item.meta) return 'icon';
  if (item.poster) return 'poster';
  if (item.kind === 'video') return 'video';
  if (item.kind === 'bg') return 'background';
  return 'img';
};

// The listed URL basename: a video keys on its media URL (the still is an opaque
// data URL); data: URLs have no meaningful basename.
const basenameOf = (item) => {
  const url = item.kind === 'video' ? (item.videoUrl || '') : (item.src || '');
  if (!url || url.startsWith('data:')) return item.kind === 'video' ? '(in-page video)' : '(inline data)';
  try {
    const u = new URL(url);
    return decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || u.hostname);
  } catch {
    return url;
  }
};

// Compact numbered listing of the scan results for the system-prompt suffix (contract
// §8): index, kind, dims, format, truncated basename/alt; capped at LISTING_LIMIT.
// `formatOfItem` is injected (lib/filters.js; a stub in tests) to keep this module pure.
export const buildListing = (items, { formatOfItem = () => '' } = {}) => {
  const all = Array.isArray(items) ? items : [];
  const lines = all.slice(0, LISTING_LIMIT).map((it, i) => {
    const parts = [`${i}: ${listingKind(it)}`];
    if (it.w > 0 && it.h > 0) parts.push(`${it.w}x${it.h}`);
    const fmt = formatOfItem(it);
    if (fmt) parts.push(fmt);
    parts.push(`"${truncate(basenameOf(it), LISTING_NAME_CHARS)}"`);
    if (it.alt) parts.push(`alt "${truncate(it.alt, LISTING_ALT_CHARS)}"`);
    return parts.join(' ');
  });
  if (all.length > LISTING_LIMIT) lines.push(`(+${all.length - LISTING_LIMIT} more not listed)`);
  return lines.join('\n');
};

// A listed tab's address, reduced to origin + path: the query/fragment hold session
// ids and search terms and don't help pick a tab. Truncating wouldn't do it — a
// length cut keeps the FRONT of a query string.
export const tabUrlForModel = (raw) => {
  const u = (raw || '').trim();
  if (!u) return '';
  try {
    const parsed = new URL(u);
    return parsed.origin + parsed.pathname;
  } catch {
    return u.split(/[?#]/)[0];
  }
};

// Compact numbered listing of the user's OTHER open tabs (contract §8) — what the
// model references in a scanTab op. Built only when the user opted in (`shareTabs`).
// Tab titles/URLs are page-controlled DATA; they ride the suffix as text only.
export const buildTabsListing = (tabs) => {
  const all = Array.isArray(tabs) ? tabs : [];
  const lines = all.slice(0, TABS_LIMIT).map((t, i) => {
    const title = truncate((t && t.title) || '(untitled)', TAB_TITLE_CHARS);
    const url = truncate(tabUrlForModel(t && t.url), TAB_URL_CHARS);
    return `${i}: "${title}"${url ? ` — ${url}` : ''}`;
  });
  if (all.length > TABS_LIMIT) lines.push(`(+${all.length - TABS_LIMIT} more not listed)`);
  return lines.join('\n');
};

// ── Dropped-attachment routing ──────────────────────────────────────────────

// Index of the scan-listing entry a dropped URL refers to, or -1 — so a matched drop
// can be referenced by index in focus/open ops afterwards. `sourceOf` keys a video on
// its media URL; `sameSource` is fragment-insensitive and '' never matches.
export const matchListingIndex = (items, url) =>
  (Array.isArray(items) ? items : []).findIndex((it) => it && sameSource(sourceOf(it), url));

// Text note describing the user's attachments, appended to the turn text so the
// model can connect the attached images back to listing indices (data, not markup).
export const attachmentNote = (attachments) => {
  const parts = (Array.isArray(attachments) ? attachments : []).map((a) => (
    Number.isInteger(a.index) && a.index >= 0
      ? `image ${a.index} from the listing${a.name ? ` (${a.name})` : ''}`
      : (a.name || 'an image')));
  return parts.length ? `[The user attached: ${parts.join('; ')}]` : '';
};

// ── open.actions → editor launch options (contract §8 translation) ──────────

// Resolve one §2 crop token to an absolute pixel coordinate on an axis of `length`
// px (mirrors browser/js/core/units.js resolveAxisPx, minus cm/in — the extension
// has no page metrics, so physical units can't be resolved here). null = unsupported.
const resolveCropToken = (tok, length) => {
  const m = /^(-?)(\d+(?:\.\d+)?|\.\d+)(%|px|cm|in)?$/.exec(String(tok));
  if (!m) return null;
  const unit = m[3] || 'px';
  if (unit === 'cm' || unit === 'in') return null;
  const v = parseFloat(m[2]);
  const px = unit === '%' ? (v / 100) * length : v;
  return m[1] === '-' ? length - px : px;
};

// Translate validated open.actions onto the existing `#stencil=` launch options:
//   crop           → payload `crop` rect (original-image pixels; needs the dims)
//   filter/layout  → a layout payload with imageFilter/filterColor/lines
//   page           → payload `page: { size }`; rotate has no slot → dropped w/ warning
// Pure. Returns { launch: { crop?, layout?, page? }, warnings }.
export const translateOpenActions = (actions, { width = 0, height = 0 } = {}) => {
  const warnings = [];
  const launch = {};
  let rect = null;     // { x1, x2, y1, y2 } crop-edge state across crop actions
  let layout = null;   // { lines, imageFilter?, filterColor? }

  for (const a of actions || []) {
    switch (a.op) {
      case 'crop': {
        if (!(width > 0 && height > 0)) {
          warnings.push('Skipped "crop" — the image dimensions are unknown');
          break;
        }
        const cur = rect || { x1: 0, x2: width, y1: 0, y2: height };
        const next = { ...cur };
        let bad = null;
        for (const [key, length] of [['x1', width], ['x2', width], ['y1', height], ['y2', height]]) {
          const tok = a.spec[key];
          if (tok == null) continue;
          const px = resolveCropToken(tok, length);
          if (px == null) { bad = tok; break; }
          next[key] = px;
        }
        if (bad != null) {
          warnings.push(`Skipped "crop" — cm/in crop units can't be resolved in the extension (token "${bad}"); crop in the editor instead`);
          break;
        }
        rect = next;
        break;
      }
      case 'rotate':
        // The launch payload has no rotation slot — rotating happens in the editor.
        warnings.push('Skipped "rotate" — the editor hand-off can\'t carry a rotation; rotate in the editor after it opens');
        break;
      case 'filter':
        layout = layout || { lines: [] };
        layout.imageFilter = a.mode;
        if (a.mode === 'custom') layout.filterColor = a.tint;
        else delete layout.filterColor;
        break;
      case 'layout':
        layout = layout || { lines: [] };
        layout.lines = layout.lines.concat(a.lines);
        break;
      case 'page':
        launch.page = { size: a.format.toUpperCase() };
        break;
      /* no default — the parser only emits the ops above */
    }
  }

  if (rect) {
    launch.crop = {   // canonical wire spelling ({w,h})
      x: Math.round(Math.min(rect.x1, rect.x2)),
      y: Math.round(Math.min(rect.y1, rect.y2)),
      w: Math.max(1, Math.round(Math.abs(rect.x2 - rect.x1))),
      h: Math.max(1, Math.round(Math.abs(rect.y2 - rect.y1))),
    };
  }
  if (layout) {
    if (width > 0 && height > 0) {
      layout.imageWidth = width;
      layout.imageHeight = height;
    }
    launch.layout = layout;
  }
  return { launch, warnings };
};

// ── The controller ──────────────────────────────────────────────────────────

// Build the controller. Injected capabilities (assistant.js wires the real ones):
//   getClient / getListing / formatOfItem / pageUrl — the client, the working listing
//     and its formatting; getTabs — the §8 tabs listing ([] omits the block).
//   focusImage / openImage / attachImage (§7 downscale) / pinImage / unpinImage /
//     openUrlImage — the per-op page/editor capabilities (throw on failure);
//     scanTab / rescan swap or refresh the working listing (gather ops).
//   setTheme / setFilters / setAccent — the panel's own controls (§8 panel settings).
//   clearChat — the surface's §10 confirm, called DEFERRED after the turn's rounds;
//     on true the controller wipes its replay history.
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
    // The §7 replay rule only ever sends the current turn's images plus the LAST image
    // of the most recent prior image-bearing turn — older base64 payloads can never
    // reach the wire again, so trim them here rather than retain them forever.
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

  // One executor per §8 op, keyed like the validators (opPlan.js EXT_VALIDATORS).
  // Each mutates the shared execution context `x` ({ listing, cards, warnings,
  // attached, attachedIndices }).
  const opExecutors = {
    focus: async (a, x) => {
      const entry = x.listing[a.image];
      let ok = false;
      try { ok = !!(await focusImage(a.image, entry)); } catch { ok = false; }
      x.cards.push({ kind: 'focus', index: a.image, entry, ok });
    },
    open: async (a, x) => {
      const entry = x.listing[a.image];
      try {
        const extra = await openImage(a, entry);
        if (Array.isArray(extra)) x.warnings.push(...extra);
        x.cards.push({ kind: 'open', index: a.image, entry, ok: true });
      } catch (err) {
        x.warnings.push(`Could not open image ${a.image}: ${err.message}`);
        x.cards.push({ kind: 'open', index: a.image, entry, ok: false });
      }
    },
    attach: async (a, x) => {
      const got = [];   // this action's successes only — the card must not repeat earlier actions'
      for (const idx of a.indices) {
        try {
          const img = await attachImage(idx, x.listing[idx]);
          if (img) { x.attached.push(img); got.push(idx); }
        } catch (err) {
          x.warnings.push(`Could not attach image ${idx}: ${err.message}`);
        }
      }
      x.attachedIndices.push(...got);
      x.cards.push({ kind: 'attach', indices: a.indices.slice(), attached: got });
    },
    pin: async (a, x) => {
      const got = [];
      for (const idx of a.indices) {
        try {
          await pinImage(idx, x.listing[idx]);
          got.push(idx);
        } catch (err) {
          x.warnings.push(`Could not pin image ${idx}: ${err.message}`);
        }
      }
      x.cards.push({ kind: 'pin', indices: a.indices.slice(), pinned: got });
    },
    // The other half of pin (§8): same per-index execution, local pins only.
    unpin: async (a, x) => {
      const got = [];
      for (const idx of a.indices) {
        try {
          await unpinImage(idx, x.listing[idx]);
          got.push(idx);
        } catch (err) {
          x.warnings.push(`Could not unpin image ${idx}: ${err.message}`);
        }
      }
      x.cards.push({ kind: 'unpin', indices: a.indices.slice(), unpinned: got });
    },
    openUrl: async (a, x) => {
      // The model may only ECHO the user: the exact URL must appear in the
      // user's own messages this conversation — page content, scan results and
      // the model itself can never introduce a host (browser §10 parity).
      const typed = history.filter((m) => m.role === 'user').map((m) => m.text).join('\n');
      if (!typed.includes(a.url)) {
        x.warnings.push(`openUrl blocked: "${a.url}" is not a URL you gave in this conversation`);
        x.cards.push({ kind: 'openUrl', url: a.url, ok: false });
        return;
      }
      try {
        await openUrlImage(a);
        x.cards.push({ kind: 'openUrl', url: a.url, incognito: !!a.incognito, ok: true });
      } catch (err) {
        x.warnings.push(`Could not open ${a.url}: ${err.message}`);
        x.cards.push({ kind: 'openUrl', url: a.url, incognito: !!a.incognito, ok: false });
      }
    },
    // ── Panel settings: the assistant driving the surface's own controls. They
    // touch nothing on the page and fetch nothing, so they carry no listing indices
    // and never gather context (no auto-continuation).
    theme: async (a, x) => {
      if (!setTheme) { x.warnings.push('Changing the theme is not supported here'); return; }
      try {
        await setTheme(a.mode);
        x.cards.push({ kind: 'theme', mode: a.mode, ok: true });
      } catch (err) {
        x.warnings.push(`Could not switch the theme: ${err.message}`);
        x.cards.push({ kind: 'theme', mode: a.mode, ok: false });
      }
    },
    accent: async (a, x) => {
      if (!setAccent) { x.warnings.push('Changing the accent is not supported here'); return; }
      const asked = a.color || a.preset;
      try {
        const res = (await setAccent(a)) || {};
        x.cards.push({ kind: 'accent', asked, applied: res.label || asked, exact: res.exact !== false, ok: true });
      } catch (err) {
        x.warnings.push(`Could not change the accent: ${err.message}`);
        x.cards.push({ kind: 'accent', asked, ok: false });
      }
    },
    filter: async (a, x) => {
      if (!setFilters) { x.warnings.push('Changing the filters is not supported here'); return; }
      try {
        const res = await setFilters(a);
        x.cards.push({ kind: 'filter', applied: (res && res.applied) || [], ok: true });
      } catch (err) {
        x.warnings.push(`Could not change the filters: ${err.message}`);
        x.cards.push({ kind: 'filter', applied: [], ok: false });
      }
    },
    scanTab: async (a, x) => {
      const tab = x.tabs[a.tab];
      let res = null;
      try { res = await scanTab(tab, a.tab); } catch (err) { res = { ok: false, error: err.message }; }
      if (res && res.ok) {
        // The owner swapped the working listing; the continuation round re-reads it.
        x.scannedTab = { index: a.tab, title: res.title || (tab && tab.title) || '', count: res.count || 0 };
        x.cards.push({ kind: 'scanTab', index: a.tab, title: x.scannedTab.title, count: x.scannedTab.count, ok: true });
      } else {
        x.warnings.push(`Could not scan tab ${a.tab}: ${(res && res.error) || 'the scan failed'}`);
        x.cards.push({ kind: 'scanTab', index: a.tab, title: (tab && tab.title) || '', ok: false });
      }
    },
    // §10 clearChat: never executed in plan order — only marked. send() shows the
    // confirm after the turn's other actions and any continuation round complete.
    clearChat: async () => { clearAsked = true; },
    // Re-scan the CURRENT page (§8): the working listing refreshes in place; a GATHER
    // op, so a rescan-only plan re-sends the turn once over the fresh listing.
    rescan: async (a, x) => {
      if (!rescan) { x.warnings.push('Re-scanning is not supported here'); return; }
      let res = null;
      try { res = await rescan(); } catch (err) { res = { ok: false, error: err.message }; }
      if (res && res.ok) {
        x.rescanned = { count: res.count || 0 };
        x.cards.push({ kind: 'rescan', count: x.rescanned.count, ok: true });
      } else {
        x.warnings.push(`Could not re-scan the page: ${(res && res.error) || 'the scan failed'}`);
        x.cards.push({ kind: 'rescan', ok: false });
      }
    },
  };

  // Execute a parsed plan's actions. Returns { cards, warnings, attached, … } where
  // `attached` is the LlmImages fetched by attach actions (for the continuation) and
  // `scannedTab` marks a successful listing swap.
  const execute = async (plan, listing, tabs) => {
    const x = { listing, tabs, cards: [], warnings: plan.warnings.slice(), attached: [], attachedIndices: [], scannedTab: null, rescanned: null };
    for (const a of plan.actions) {
      if (rejectForbidden(a, x)) continue;
      await opExecutors[a.op]?.(a, x);
    }
    return x;
  };

  // One model round: replay history → chat → parse → execute. `continued` marks the
  // bounded auto-continuation round (contract §8: at most ONE per user turn). The
  // listing is re-read each round (a scanTab swap feeds the continuation the new set);
  // the tabs snapshot stays fixed for the whole turn (index stability).
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

    // One user turn. `attachments` are user-dropped images already encoded as LlmImages
    // (a listing `index` marks a drop matched to a scanned entry). Returns { reply,
    // warnings, cards, chatOnly, continuation? }; typed LlmErrors and invalid-plan
    // errors propagate for the page to render as chat errors (never parsed as plans).
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
