// ── Chat controller: history, attachments, and the send loop ────────────────
// Owns the client-side conversation state (contract §7): history is replayed in
// full on every call (bounded to 32 messages), images follow the replay rule, and
// attachments are downscaled before base64-encoding. Every capability that needs a
// DOM (canvas downscale, video frames, image export) is INJECTED so `node --test`
// can drive the controller with stubs.
import { EDITOR_SYSTEM_PROMPT, LLM_SYSTEM_PROMPT, parseOpPlan, executeOpPlan, renderAskPreviews } from './opPlan.js';
import { CONTINUATION_NOTE } from './chatStore.js';
// §7: appended to a turn's system prompt when — and only when — that turn actually
// attaches the working image's edge map.
export const EDGE_MAP_SENTENCE = 'The second attached image is an edge-map render of the working image at the same pixel coordinates: use it to place outline points on real edges.';
import { isVideoFile } from '../core/videoFrame.js';
import { core } from '../core/stencilCore.js';
import { applyContourRGBA } from '../core/contourFilter.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { scaledDataUrl } from '../utils.js';

export const HISTORY_LIMIT = 32;
export const MAX_IMAGE_EDGE = 1568;
// How many images ONE message may carry: replayed and paid for per turn (§7); past this
// the queue is refused with a message rather than silently trimmed on the way out.
export const MAX_ATTACHMENTS = 3;

// Window-event name the chat UIs listen on to repaint the composer's attachment
// chips. Lives here (DOM-free constant) so the session glue and the views share it.
export const CHAT_ATTACHMENTS_EVENT = 'stencil:chat-attachments-changed';

// §2.1 `save` with no name: the attachment the plan is working on names the project.
// Empty means "no idea from here" — the surface falls back to the editor's own name.
export const attachmentSaveName = (attachment) =>
  String(attachment?.name || '').replace(/\.[^.]+$/, '').trim();

// Does this plan actually WORK ON the picture? Only then is an attached image worth
// adopting as the working one — a question/settings-only plan leaves the editor alone.
// Variants count: they are alternatives OF the working image.
const IMAGE_OPS = new Set(['crop', 'rotate', 'filter', 'layout', 'page', 'blank', 'clear', 'frame']);
export const planEditsTheImage = (plan) =>
  !!plan && ((plan.actions || []).some((a) => IMAGE_OPS.has(a.op)) || (plan.variants || []).length > 0);

export const VIDEO_FRAME_COUNT = 4;
// Ask-option previews live in the shared chat log for the whole session but render
// as small thumbs (no download/open affordance) — stored at thumbnail size.
export const ASK_PREVIEW_MAX_EDGE = 256;

// data:mediaType;base64,payload → { mediaType, data } (the LlmImage wire shape).
export const splitDataUrl = (u) => {
  const m = /^data:([^;,]+);base64,(.*)$/s.exec(String(u || ''));
  return m ? { mediaType: m[1], data: m[2] } : null;
};

// Downscale an image blob/File to ≤ maxEdge px on the long edge and re-encode as
// PNG (contract §7). Browser-only default for the injected prepareAttachment.
export const downscaleImageToDataUrl = async (blob, maxEdge = MAX_IMAGE_EDGE) => {
  const bmp = await createImageBitmap(blob);
  const url = scaledDataUrl(bmp, bmp.width, bmp.height, maxEdge, 'image/png');
  bmp.close?.();
  return url;
};

// Re-encode a data URL at thumbnail size (browser default for the injected
// previewThumb). Best-effort: any failure keeps the original URL.
const thumbnailDataUrl = async (dataUrl, maxEdge = ASK_PREVIEW_MAX_EDGE) => {
  try {
    const img = new Image();
    img.src = dataUrl;
    await img.decode();
    return scaledDataUrl(img, img.naturalWidth, img.naturalHeight, maxEdge, 'image/jpeg', 0.85);
  } catch {
    return dataUrl;
  }
};

// The core contour pass in place over RGBA8 pixels — wasm stencil_applyContourRGBA
// when loaded, else the JS reference. Shared with chatSession's crop-edge wiring.
export const contourInPlace = (data, w, h) => (core.op('applyContourRGBA') || applyContourRGBA)(data, w, h);

// Re-render a snapshot data URL with the core `contour` filter — the exact path the
// user-facing "contour" filter mode uses. Browser-only default for the injected edgeMap.
export const contourDataUrl = async (dataUrl) => {
  const img = new Image();
  img.src = dataUrl;
  await img.decode();
  const canvas = document.createElement('canvas');
  canvas.width = img.naturalWidth;
  canvas.height = img.naturalHeight;
  const ctx = canvas.getContext('2d');
  ctx.drawImage(img, 0, 0);
  const d = ctx.getImageData(0, 0, canvas.width, canvas.height);
  contourInPlace(d.data, canvas.width, canvas.height);
  ctx.putImageData(d, 0, 0);
  return canvas.toDataURL('image/png');
};

// A provider/model that has no vision, answering the auto-attached working image.
// Providers word it differently, so match the shapes rather than one string.
export const isImageRejection = (err) =>
  /multimodal|vision|image input|does not support image|image_url|images are not/i.test(
    String(err?.message ?? err ?? ''));

// Image replay rule (contract §7): the current turn keeps its images; of the PRIOR
// turns only the single most recent image survives; older turns replay text-only.
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

// §7 auto-continuation: ops that load a picture the model has not seen yet. A plan
// containing one — that drew NO layout — is re-sent once with the new working image
// attached: outlining needs pixels, and a plan that already placed lines committed to
// its coordinates. `openUrl` counts with OR without incognito (adopted in this editor).
export const LOAD_ONLY_OPS = new Set(['openUrl', 'blank', 'frame']);
export const planLoadsWithoutTracing = (plan) =>
  !!plan && Array.isArray(plan.actions)
  && plan.actions.some((a) => LOAD_ONLY_OPS.has(a.op))
  && !plan.actions.some((a) => a.op === 'layout');

// Build the controller. Every DOM-needing capability is injected; getClient is re-read
// per send so settings changes take effect; savedServers is the pool §10 `connect`
// resolves in; workingSnapshot (default: exportImage downscaled) is the §7 auto-attach;
// edgeMap renders that snapshot with the core contour filter (§7, best-effort).
export const createChatController = ({
  stencil,
  getClient,
  exportImage,
  prepareAttachment = downscaleImageToDataUrl,
  extractFrames,
  frameAt,
  loadImage = async (dataUrl, name) => { await stencil.load(dataUrl, { name }); },
  savedServers = loadSavedServers,
  previewThumb = thumbnailDataUrl,
  openIncognito,
  workingSnapshot,
  edgeMap = contourDataUrl,
  onAttachmentsChanged,   // fired when send drains the queued attachments (chip repaint)
  saveProject,            // §2.1 `save` op: async (name) => persist the working image + layout
  copyRendered,           // §10 `copy` op: () => promise of the clipboard write's real outcome
  copyLayoutRendered,     // §10 copy what:"layout": () => promise of the layout-JSON write's outcome
  removeProjectNamed,     // §10 removeProject: async (name) => note-string | null (confirms in-app)
  clearWorkingImage,      // §10 removeProject current:true with nothing saved: async () =>
                          // note-string | null — the `clear` flow behind the same confirm
  clearLocalProjects,     // §10 clearProjects: async () => note-string | null (confirms in-app)
  renameActiveProject,    // §10 renameProject: async (name) => note-string | null (store errors as notes)
  setBlankColor,          // §10 blankColor: async (color) => note-string | null (non-blank → note)
  openProjectNamed,       // §10 openProject: async (name) => note-string | null (confirms in-app)
  clearChatConversation,  // §10 clearChat: async () => note-string | null (confirms in-app; executor-deferred)
} = {}) => {
  const history = [];       // [{ role, text, images? }] — canonical wire shape
  // This turn's user images, kept past the queue drain so an EDITING plan can adopt
  // one as the working image when the editor is empty (see the adoption below).
  let turnAttachments = [];
  const attachments = [];   // pending for the next send: { name, kind, use, dataUrl?, frames?, file? }
  let videoInput = null;    // the current working-video attachment (frame ops valid)
  // Latched once a provider rejects images (see send): a text-only model must still be
  // able to plan text-only edits, so we stop auto-attaching instead of failing forever.
  let textOnlyModel = false;

  // The working image for this turn, downscaled to the attachment limit. Deliberately
  // NOT the injected previewThumb — that one is the ask-card thumbnailer, and swapping
  // it must not change what the model sees.
  const snapshot = workingSnapshot
    || (exportImage ? async () => thumbnailDataUrl(await exportImage(), MAX_IMAGE_EDGE) : null);

  // The §7 edge map for a just-taken snapshot: same pixels, contour-filtered — so its
  // coordinates line up with the snapshot's. Best-effort: no edge map is never fatal.
  const edgeOf = async (snapshotUrl) => {
    try { return splitDataUrl(await edgeMap(snapshotUrl)); } catch { return null; }
  };

  const pushHistory = (msg) => {
    // Reclaim replay-dead image payloads before an image-bearing turn lands: of the
    // prior turns replayMessages only ever sends the single most recent image, so
    // keep exactly that one and drop the rest (the wire output stays identical).
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

  // The editor prompt (§4 + the §10 settings-op block) plus a short dynamic suffix
  // (contract §4 allows appending). The §7 edge-map sentence rides the suffix when —
  // and only when — this turn actually attached the edge map.
  const buildSystem = (withEdgeMap = false) => {
    let s = EDITOR_SYSTEM_PROMPT;
    const size = stencil?.imageSize;
    if (size) s += `\n\nCurrent working image: ${size.width}x${size.height} px.`;
    if (videoInput) s += `\nThe current input is a video (${videoInput.name}) — "frame" ops are valid.`;
    if (withEdgeMap) s += `\n${EDGE_MAP_SENTENCE}`;
    return s;
  };

  const controller = {
    history,
    attachments,
    get videoInput() { return videoInput; },

    // Queue a File for the next send. Images are downscaled now; videos are sampled
    // into frames (the video bytes themselves never go to the LLM). `use` starts as
    // 'analyze'; the panel can flip it to 'working' (load as the working image/video).
    async addAttachment(file) {
      if (attachments.length >= MAX_ATTACHMENTS) {
        throw new Error(`up to ${MAX_ATTACHMENTS} images per message`);
      }
      const type = (file && file.type) || '';
      if (isVideoFile(file)) {
        if (!extractFrames) throw new Error('Video attachments are not supported here');
        const frames = await extractFrames(file, VIDEO_FRAME_COUNT);
        attachments.push({ name: file.name || 'video', kind: 'video', use: 'analyze', frames, file });
      } else if (type.startsWith('image/')) {
        const dataUrl = await prepareAttachment(file);
        attachments.push({ name: file.name || 'image', kind: 'image', use: 'analyze', dataUrl });
      } else {
        throw new Error(`Not an image or video (got "${type || 'unknown'}")`);
      }
      return attachments[attachments.length - 1];
    },
    // Queue an already-encoded image (a base64 data: URL) for the next send — the
    // scripting path (stencil.prompt({ images })) shares the panel's attachment flow.
    addImageDataUrl(dataUrl, name = 'image') {
      if (attachments.length >= MAX_ATTACHMENTS) {
        throw new Error(`up to ${MAX_ATTACHMENTS} images per message`);
      }
      if (!splitDataUrl(dataUrl)) throw new Error('images must be base64 data: URLs');
      attachments.push({ name, kind: 'image', use: 'analyze', dataUrl });
      return attachments[attachments.length - 1];
    },
    removeAttachment(i) { attachments.splice(i, 1); },
    // Start a fresh conversation: model history, queued attachments and the
    // working-video binding all go (settings and the working image stay).
    clearConversation() {
      history.length = 0;
      attachments.length = 0;
      videoInput = null;
      // A fresh conversation re-tries the working-image attachment: the user may well
      // have switched to a vision model between the two.
      textOnlyModel = false;
    },
    // Replace the conversation with a RESTORED one (contract §12): text-only turns in
    // display form — §7 permits replaying either raw model text or the extracted reply.
    // Attachments and the working video are per-session — a restore starts without them.
    seedHistory(messages) {
      history.length = 0;
      attachments.length = 0;
      videoInput = null;
      for (const m of messages || []) {
        if ((m?.role === 'user' || m?.role === 'assistant') && typeof m?.text === 'string' && m.text) {
          history.push({ role: m.role, text: m.text });
        }
      }
      if (history.length > HISTORY_LIMIT) history.splice(0, history.length - HISTORY_LIMIT);
    },
    setAttachmentUse(i, use) { if (attachments[i]) attachments[i].use = use; },
    // Retry resends the whole turn: re-queue the drained analyze-attachments
    // (no-op if the user queued something new; video stays bound separately).
    requeueLastTurnAttachments() {
      if (attachments.length) return;
      for (const at of turnAttachments) {
        if (attachments.length >= MAX_ATTACHMENTS) break;
        attachments.push({ name: at.name, kind: 'image', use: 'analyze', dataUrl: at.dataUrl });
      }
    },

    // One turn: build messages → client.chat → parseOpPlan → executeOpPlan.
    // Returns { reply, warnings, results, chatOnly }; typed LlmErrors and invalid-plan
    // errors propagate for the panel to render as chat errors (never parsed as plans).
    async send(text, { signal } = {}) {
      const client = getClient();

      const images = [];
      // Ground the turn in what the editor is showing: the working image rides along
      // automatically, ahead of the user's own attachments. Without it a request about
      // the picture is answered from imagination instead of pixels.
      const preWarnings = [];
      // The §7 edge map of this turn's snapshot. Wire-only: it rides the request
      // directly after the snapshot but never enters history (see respond).
      let edgeImage = null;
      if (snapshot && !textOnlyModel && stencil?.imageSize) {
        try {
          const url = await snapshot();
          const img = splitDataUrl(url);
          if (img) {
            images.push(img);
            edgeImage = await edgeOf(url);
          }
        } catch {
          // An empty editor (or a canvas that won't export) simply sends no snapshot —
          // the turn is still a valid text conversation.
        }
      }
      const autoAttached = images.length > 0;
      // THIS turn's user attachments, kept past the queue drain: an editing plan on an
      // empty editor adopts the first one as the working image. Kept across turns that
      // queue nothing new — an ask-card answer arrives attachment-less, and the editing
      // plan it triggers must still be able to adopt the previous turn's image.
      if (attachments.length) turnAttachments = [];

      // Consume pending attachments: 'working' images load into the editor; every
      // image (and every sampled video frame) also attaches to this turn.
      for (const at of attachments) {
        if (at.kind === 'image') {
          if (at.use === 'working') await loadImage(at.dataUrl, at.name);
          else turnAttachments.push({ dataUrl: at.dataUrl, name: at.name });
          const img = splitDataUrl(at.dataUrl);
          if (img) images.push(img);
        } else {
          if (at.use === 'working') videoInput = at;
          for (const f of at.frames || []) {
            const img = splitDataUrl(f);
            if (img) images.push(img);
          }
        }
      }
      attachments.length = 0;
      // The queue just drained into the message — let the composer drop its chips.
      onAttachmentsChanged?.();

      const userMsg = { role: 'user', text: String(text ?? '') };
      if (images.length) userMsg.images = images;
      pushHistory(userMsg);
      return respond({ signal, preWarnings, autoAttached, continued: false, edgeImage, deferred: [] });
    },
  };

  // §10 clearChat lands at the turn's true END — after the §7 continuation round
  // and the §3 passes, not at the end of the round that asked. Both rounds feed
  // the same `deferred` list; dedup by op so a clearChat asked twice confirms once.
  const flushDeferred = async (deferred, warnings) => {
    if (!deferred.length) return;
    const seen = new Set();
    const actions = deferred.filter((a) => !seen.has(a.op) && seen.add(a.op));
    // clearChat is the only deferred op, so only its capability rides along.
    const { warnings: w } = await executeOpPlan({ actions, variants: [], warnings: [] }, stencil, { clearChatConversation });
    warnings.push(...w);
  };

  // One model round against the current history. Split out of send() so an
  // auto-continuation (§7) can run a second one with the freshly loaded image in place.
  const respond = async ({ signal, preWarnings, autoAttached, continued, edgeImage = null, deferred = [] }) => {
      const client = getClient();
      const messages = replayMessages(history);
      // §7 edge map: spliced into the WIRE copy of this turn's message, directly after
      // the working snapshot (images[0]). History never holds it, so the replay rule's
      // "single most recent prior image" keeps picking a real snapshot/attachment.
      const withEdgeMap = !!edgeImage && !textOnlyModel;
      if (withEdgeMap) {
        const last = messages[messages.length - 1];
        last.images = [last.images[0], edgeImage, ...last.images.slice(1)];
      }

      let raw;
      try {
        raw = await client.chat({ system: buildSystem(withEdgeMap), messages, signal });
      } catch (err) {
        // A text-only model rejects the auto-attached snapshot. Stop attaching it for
        // the rest of the session, strip the images out of the replayed history, and
        // retry ONCE — "make it sepia" must still work on a model without vision.
        if (!autoAttached || !isImageRejection(err)) throw err;
        textOnlyModel = true;
        for (const m of history) delete m.images;
        preWarnings.push('this model is text-only, so the picture was not sent — pick a vision model to ask about the image itself');
        raw = await client.chat({ system: buildSystem(), messages: replayMessages(history), signal });
      }
      // Replay the RAW model text as the assistant turn so the model keeps answering
      // in pure-JSON form; the parsed `reply` is what the user sees.
      pushHistory({ role: 'assistant', text: raw });

      const plan = parseOpPlan(raw);
      // A chat-only fallback that LOOKS like a plan (mangled JSON) or like
      // MARKUP (the model answered in HTML instead of ops) gets an explicit
      // note — a bare blob otherwise reads as "the assistant did nothing".
      if (plan.chatOnly && /^\s*[{`]/.test(raw) && /"(op|actions|version)"/.test(raw)) {
        plan.warnings.push('That answer looks like a plan, but its JSON is malformed — nothing was executed. Retry, or switch to a larger model.');
      } else if (plan.chatOnly && /^\s*</.test(raw) && /<\w+[\s>]/.test(raw)) {
        plan.warnings.push('The model answered with markup instead of a Stencil plan — nothing was executed. Retry, or switch to a larger model.');
      }
      // An EDITING plan with nothing on the canvas adopts the just-attached picture as
      // the working image ("make it b&w" arriving with a photo). Only when the editor is
      // empty: with an image already open, the attachment stays a reference (§7).
      if (!autoAttached && turnAttachments.length && planEditsTheImage(plan)) {
        const adopt = turnAttachments[0];
        try {
          await loadImage(adopt.dataUrl, adopt.name);
          preWarnings.push(`opened ${adopt.name} in the editor first — the actions ran on it`);
        } catch {
          // Loading failed: fall through and let the ops report their own trouble.
        }
      }
      const loadFrame = (videoInput && frameAt)
        ? async (idx) => { await loadImage(await frameAt(videoInput.file, idx), `frame${idx}`); }
        : null;
      // §10 openUrl guard: the pool of URLs the model may echo — the user's OWN
      // messages this conversation (assistant/replayed text never counts).
      const userText = () => history.filter((m) => m.role === 'user').map((m) => m.text).join('\n');
      // §2.1: the turn's attachments, 1-based, as the `image` op indexes them — the
      // same list the empty-canvas adoption above draws from. The one currently loaded
      // names an unnamed `save`, so a 3-image plan yields 3 distinctly named projects.
      let activeAttachment = turnAttachments.length === 1 ? turnAttachments[0] : null;
      const loadAttachment = async (index) => {
        const at = turnAttachments[index - 1];
        if (!at) throw new Error(`this message attached ${turnAttachments.length} image(s)`);
        await loadImage(at.dataUrl, at.name);
        activeAttachment = at;
      };
      const { results, warnings } = await executeOpPlan(plan, stencil, {
        exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage, clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, clearChatConversation, deferredSink: deferred,
        saveProject: saveProject
          ? (name) => saveProject(name || attachmentSaveName(activeAttachment))
          : null,
      });
      // §7 auto-continuation: the plan loaded a picture the model has not seen (and drew
      // no layout) — re-send once with the new image attached, so "load, crop, b&w and
      // outline the face" works in one message.
      if (!continued && planLoadsWithoutTracing(plan) && snapshot && !textOnlyModel && stencil?.imageSize) {
        // Wire-only (§7): the persistence layer owns this string and refuses to store
        // it, so a restored transcript can never show the machinery (§12.1).
        const note = { role: 'user', text: CONTINUATION_NOTE };
        let nextEdge = null;
        try {
          const url = await snapshot();
          const img = splitDataUrl(url);
          if (img) {
            note.images = [img];
            nextEdge = await edgeOf(url);   // fresh edge map for the fresh image, wire-only
          }
        } catch { /* no snapshot → the note alone still moves the turn along */ }
        if (note.images) {
          pushHistory(note);
          const next = await respond({ signal, preWarnings, autoAttached: true, continued: true, edgeImage: nextEdge, deferred });
          // The continuation round finished — NOW the turn is over: run what both
          // rounds deferred, folding its notes into the merged warnings.
          const combined = warnings.concat(next.warnings);
          await flushDeferred(deferred, combined);
          return { ...next, warnings: combined, results: results.concat(next.results) };
        }
      }
      // §3.0: the turn ends HERE — no post-plan model round follows the reply.
      // §11: a plan may also ASK. Its option previews render AFTER the edits ran (the
      // choice shows against the image as it now is) and against a copy, so the working
      // image is untouched by the question itself.
      const { previews, warnings: askWarnings } =
        await renderAskPreviews(plan.ask, stencil, { exportImage, loadFrame });
      // The card shows small thumbs only — never store the full-resolution exports.
      const askPreviews = [];
      for (const p of previews) askPreviews.push({ ...p, dataUrl: await previewThumb(p.dataUrl) });
      // Outermost round only — a continued round hands its deferred actions back
      // to the caller, which flushes them once the whole turn is over.
      if (!continued) await flushDeferred(deferred, warnings);
      return {
        reply: plan.reply, warnings: preWarnings.concat(warnings, askWarnings), results,
        ask: plan.ask, askPreviews, chatOnly: plan.chatOnly,
      };
  };
  return controller;
};
