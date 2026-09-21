// The pieces of one chat turn (llm-contract.md §7): its limits, the image encoders an
// attachment goes through, the history replay rule and what a plan says about the picture.
import PROMPT_ASSET from '../../config/llm/systemPrompt.json' with { type: 'json' };
import EVENTS from '../../config/events.json' with { type: 'json' };
import { downscaleToDataUrl, contourToDataUrl } from '../../worker/imageTasks.js';

export const HISTORY_LIMIT = 32;
export const MAX_IMAGE_EDGE = 1568;
// How many images ONE message may carry: replayed and paid for per turn (§7); past this
// the queue is refused with a message rather than silently trimmed on the way out.
export const MAX_ATTACHMENTS = 3;
// §7: appended to a turn's system prompt when — and only when — that turn attaches the
// working image's edge map. Verbatim from the shared prose asset every surface embeds.
export const EDGE_MAP_SENTENCE = PROMPT_ASSET.edgeMapSentence;
// Window-event the chat UIs listen on to repaint the composer's attachment chips.
export const CHAT_ATTACHMENTS_EVENT = EVENTS.chatAttachmentsChanged;

// §2.1 `save` with no name: the attachment the plan is working on names the project.
// Empty means "no idea from here" — the surface falls back to the editor's own name.
export const attachmentSaveName = (attachment) =>
  String(attachment?.name || '').replace(/\.[^.]+$/, '').trim();

// Does this plan work ON the picture? Only then is an attached image worth adopting as the
// working one. Variants count: they are alternatives OF the working image.
const IMAGE_OPS = new Set(['crop', 'rotate', 'filter', 'layout', 'page', 'blank', 'clear', 'frame']);
export const planEditsTheImage = (plan) =>
  !!plan && ((plan.actions || []).some((a) => IMAGE_OPS.has(a.op)) || (plan.variants || []).length > 0);

export const VIDEO_FRAME_COUNT = 4;
// Ask-option previews live in the shared chat log for the whole session but render
// as small thumbs (no download/open affordance) — stored at thumbnail size.
const ASK_PREVIEW_MAX_EDGE = 256;

// data:mediaType;base64,payload → { mediaType, data } (the LlmImage wire shape).
export const splitDataUrl = (u) => {
  const m = /^data:([^;,]+);base64,(.*)$/s.exec(String(u || ''));
  return m ? { mediaType: m[1], data: m[2] } : null;
};

// Downscale an image blob/File to ≤ maxEdge px on the long edge and re-encode as
// PNG (contract §7) — in the image worker. Browser-only default for prepareAttachment.
export const downscaleImageToDataUrl = async (blob, maxEdge = MAX_IMAGE_EDGE) => {
  const bmp = await createImageBitmap(blob);
  try {
    return await downscaleToDataUrl(bmp, bmp.width, bmp.height, { maxEdge, type: 'image/png' });
  } finally {
    bmp.close?.();
  }
};

// Re-encode a data URL at thumbnail size (browser default for the injected
// previewThumb). Best-effort: any failure keeps the original URL.
export const thumbnailDataUrl = async (dataUrl, maxEdge = ASK_PREVIEW_MAX_EDGE) => {
  try {
    const img = new Image();
    img.src = dataUrl;
    await img.decode();
    return await downscaleToDataUrl(img, img.naturalWidth, img.naturalHeight, { maxEdge, type: 'image/jpeg', quality: 0.85 });
  } catch {
    return dataUrl;
  }
};

export const contourDataUrl = async (dataUrl) => {
  const img = new Image();
  img.src = dataUrl;
  await img.decode();
  const canvas = document.createElement('canvas');
  canvas.width = img.naturalWidth;
  canvas.height = img.naturalHeight;
  const ctx = canvas.getContext('2d');
  ctx.drawImage(img, 0, 0);
  return contourToDataUrl(() => ctx.getImageData(0, 0, canvas.width, canvas.height));
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

// §7 auto-continuation: ops that load a picture the model has not seen yet. `openUrl` counts
// with OR without incognito (adopted in this editor).
const LOAD_ONLY_OPS = new Set(['openUrl', 'blank', 'frame']);
export const planLoadsWithoutTracing = (plan) =>
  !!plan && Array.isArray(plan.actions)
  && plan.actions.some((a) => LOAD_ONLY_OPS.has(a.op))
  && !plan.actions.some((a) => a.op === 'layout');
