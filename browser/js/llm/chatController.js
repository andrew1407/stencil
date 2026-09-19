// ── Chat controller: history, attachments, and the send loop ────────────────
// Owns the client-side conversation state (contract §7): history is replayed in full on
// every call (bounded to 32 messages), images follow the replay rule, and attachments are
// downscaled before base64-encoding. Every DOM capability is INJECTED for `node --test`.
import { isVideoFile } from '../core/videoFrame.js';
import { loadSavedServers } from '../net/connectionStore.js';
import { HISTORY_LIMIT, MAX_ATTACHMENTS, MAX_IMAGE_EDGE, VIDEO_FRAME_COUNT, contourDataUrl,
  downscaleImageToDataUrl, splitDataUrl, thumbnailDataUrl } from './chatTurn.js';
import { createResponder } from './chatRespond.js';
export { CHAT_ATTACHMENTS_EVENT, EDGE_MAP_SENTENCE, HISTORY_LIMIT, MAX_ATTACHMENTS, MAX_IMAGE_EDGE,
  VIDEO_FRAME_COUNT, contourDataUrl, downscaleImageToDataUrl, planEditsTheImage,
  planLoadsWithoutTracing, replayMessages, splitDataUrl } from './chatTurn.js';

// Every DOM-needing capability is injected; getClient is re-read per send so settings changes
// take effect; savedServers is the pool §10 `connect` resolves in.
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
  setChatPlacement,       // §10 chat: async ({open, dock}) => note-string | null (the panel's own buttons)
  openDialog,             // §10 dialog: async (name|null) => note-string | null (null closes; executor-deferred)
  clearChatConversation,  // §10 clearChat: async () => note-string | null (confirms in-app; executor-deferred)
  setVoiceChat,           // §10 voiceChat: (on) => note-string | null (browser-only; unsupported → note)
} = {}) => {
  const history = [];       // [{ role, text, images? }] — canonical wire shape
  const attachments = [];   // pending for the next send: { name, kind, use, dataUrl?, frames?, file? }
  // Shared by the send loop and the model round: this turn's images (kept past the queue drain so
  // an EDITING plan can adopt one), the video binding, and a text-only provider's latch.
  const state = { turnAttachments: [], videoInput: null, textOnlyModel: false };

  // Deliberately NOT the injected previewThumb — that one is the ask-card thumbnailer, and
  // swapping it must not change what the model sees.
  const snapshot = workingSnapshot
    || (exportImage ? async () => thumbnailDataUrl(await exportImage(), MAX_IMAGE_EDGE) : null);

  // The §7 edge map for a just-taken snapshot: same pixels, contour-filtered — so its
  // coordinates line up with the snapshot's. Best-effort: no edge map is never fatal.
  const edgeOf = async (snapshotUrl) => {
    try { return splitDataUrl(await edgeMap(snapshotUrl)); } catch { return null; }
  };

  const pushHistory = (msg) => {
    // Of the prior turns replayMessages only ever sends the single most recent image, so keep
    // exactly that one and drop the rest; the wire output stays identical.
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

  // The model round itself (§7), handed the mutable turn state and the §10 capability bag.
  const respond = createResponder({
    stencil, state, history, pushHistory, edgeOf, snapshot, getClient, loadImage, frameAt,
    exportImage, previewThumb, savedServers, openIncognito,
    caps: { saveProject, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage,
      clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, setChatPlacement,
      openDialog, clearChatConversation, setVoiceChat },
  });

  const controller = {
    history,
    attachments,
    get videoInput() { return state.videoInput; },

    // Images are downscaled now; videos are sampled into frames — the video bytes themselves
    // never go to the LLM.
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
      state.videoInput = null;
      // A fresh conversation re-tries the working-image attachment: the user may well
      // have switched to a vision model between the two.
      state.textOnlyModel = false;
    },
    // Replace the conversation with a RESTORED one (§12): text-only turns in display form (§7
    // permits raw model text or the extracted reply). Attachments are per-session.
    seedHistory(messages) {
      history.length = 0;
      attachments.length = 0;
      state.videoInput = null;
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
      for (const at of state.turnAttachments) {
        if (attachments.length >= MAX_ATTACHMENTS) break;
        attachments.push({ name: at.name, kind: 'image', use: 'analyze', dataUrl: at.dataUrl });
      }
    },

    // Typed LlmErrors and invalid-plan errors propagate for the panel to render as chat errors,
    // never parsed as plans.
    async send(text, { signal } = {}) {
      const client = getClient();

      const images = [];
      // The working image rides along automatically, ahead of the user's own attachments.
      const preWarnings = [];
      // The §7 edge map of this turn's snapshot. Wire-only: it rides the request
      // directly after the snapshot but never enters history (see respond).
      let edgeImage = null;
      if (snapshot && !state.textOnlyModel && stencil?.imageSize) {
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
      // THIS turn's attachments, kept past the queue drain and across turns that queue nothing new:
      // an ask-card answer arrives attachment-less, and its editing plan must still adopt the image.
      if (attachments.length) state.turnAttachments = [];

      // Consume pending attachments: 'working' images load into the editor; every
      // image (and every sampled video frame) also attaches to this turn.
      for (const at of attachments) {
        if (at.kind === 'image') {
          if (at.use === 'working') await loadImage(at.dataUrl, at.name);
          else state.turnAttachments.push({ dataUrl: at.dataUrl, name: at.name });
          const img = splitDataUrl(at.dataUrl);
          if (img) images.push(img);
        } else {
          if (at.use === 'working') state.videoInput = at;
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

  return controller;
};
