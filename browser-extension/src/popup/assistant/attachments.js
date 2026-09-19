// ── Pending dropped attachments (ride the next send) ────────────────────────
// The composer's tray: what a drop/paste queued for the next message, plus the Clear
// button's enabled state (it reads both the transcript and this queue).
import { fetchAsDataUrl, filenameFromUrl } from '../../lib/stencil.js';
import { editableSrc } from '../../lib/imageModel.js';
import { guessKindFromUrl } from '../../lib/dragUrl.js';
import { isAllowedImageUrl } from '../../lib/urlGuard.js';
import { splitDataUrl, matchListingIndex, MAX_ATTACHMENTS } from '../../llm/chatController.js';
import { isVideoFile } from '../../lib/chatDrop.js';
import { sampleVideoFrames } from '../../lib/videoFrames.js';
import { wireThumbPreview } from '../../lib/chatUi.js';
import { setTip } from '../../lib/tip.js';
import { chatLeave, entryName, toLlmImage } from './shared.js';

export const createAttachments = ({ trayEl, transcriptEl, clearBtn, getItems, getPageUrl,
                                    addWarn, attachImage, isBusy }) => {
  // Each entry: { image: { mediaType, data }, name, index? } — `index` set when the
  // dropped URL matched a scan entry (the model can then focus/open it by index).
  const pending = [];

  const renderTray = () => {
    trayEl.innerHTML = '';
    trayEl.hidden = pending.length === 0;
    pending.forEach((p, i) => {
      const chip = document.createElement('div');
      chip.className = 'chip';
      const img = document.createElement('img');
      img.alt = '';
      img.src = `data:${p.image.mediaType};base64,${p.image.data}`;
      wireThumbPreview(img, { caption: p.name });   // the chip's 28px thumb, shown big on hover
      const name = document.createElement('span');
      name.className = 'chip-name';
      const label = Number.isInteger(p.index) ? `image #${p.index} from the page (${p.name})` : p.name;
      name.textContent = label;
      setTip(name, label);   // the chip ellipsises it; the full name lives on the tooltip
      const x = document.createElement('button');
      x.className = 'chip-x';
      x.textContent = '×';
      setTip(x, 'Remove', { label: true });
      // The chip scatters before the tray rebuilds without it.
      x.addEventListener('click', () => chatLeave(x.parentElement, () => {
        pending.splice(i, 1); renderTray(); syncClearBtn();
      }));
      // Thumbnail + name + remove — no "analyze" badge: every queued image is
      // analysed, so it would say the same thing on every chip (browser parity).
      chip.append(img, name, x);
      trayEl.appendChild(chip);
    });
  };

  const addPending = (p) => {
    // One message carries at most MAX_ATTACHMENTS images (chatController.js) — the
    // extra is refused out loud rather than queued and silently dropped later.
    if (pending.length >= MAX_ATTACHMENTS) {
      addWarn(`Up to ${MAX_ATTACHMENTS} images per message — remove one first.`);
      return;
    }
    pending.push(p);
    renderTray();
    syncClearBtn();
  };

  // Videos never go to the LLM (contract §7). Same scheme allowlist as fetchAsDataUrl: host
  // permissions make fetch() all-powerful, so only http(s)/blob/data media URLs are pulled.
  const addPendingVideoUrl = async (url, baseName = '', pageUrl = '') => {
    if (!/^(https?|blob|data):/i.test(url)) throw new Error('unsupported URL scheme');
    // Scanned/dropped media URL — same SSRF guard as fetchAsDataUrl (urlGuard.js),
    // with the same scanned-page same-host carve-out.
    if (!isAllowedImageUrl(url, { allowSameHostAs: pageUrl })) throw new Error('blocked private or internal address');
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const frames = await sampleVideoFrames(await resp.blob());
    const base = baseName || filenameFromUrl(url, 'video');
    frames.forEach((f, i) => addPending({ image: splitDataUrl(f), name: `${base} — frame ${i + 1}` }));
  };

  // A URL matching a scan entry is registered AS that entry, so focus/open by index keeps
  // working; anything else is attached as a plain vision image (analysis only).
  const addPendingUrl = async (url) => {
    const items = getItems() || [];
    const idx = matchListingIndex(items, url);
    if (idx >= 0) {
      const entry = items[idx];
      // A video row attaches its captured still (which keeps the listing index, so
      // focus/open still work on it); with no still at all, sample the media itself.
      if (entry.kind === 'video' && !editableSrc(entry) && entry.videoUrl) {
        await addPendingVideoUrl(entry.videoUrl, entryName(entry), entry.resource || getPageUrl());
        return;
      }
      addPending({ image: await attachImage(idx, entry), name: entryName(entry), index: idx });
      return;
    }
    if (guessKindFromUrl(url) === 'video') {
      await addPendingVideoUrl(url, '', getPageUrl());
      return;
    }
    const dataUrl = await fetchAsDataUrl(url, { pageUrl: getPageUrl() });
    addPending({ image: await toLlmImage({ dataUrl }), name: filenameFromUrl(url) });
  };

  // A dropped/pasted local file: images attach directly; videos are sampled into frames.
  const addPendingFile = async (file) => {
    if (isVideoFile(file)) {
      const frames = await sampleVideoFrames(file);
      const base = (file.name || 'video').replace(/\.[^.]+$/, '');
      frames.forEach((f, i) => addPending({ image: splitDataUrl(f), name: `${base} — frame ${i + 1}` }));
      return;
    }
    if (!(file.type || '').startsWith('image/')) throw new Error(`not an image or video (got "${file.type || 'unknown'}")`);
    addPending({ image: await toLlmImage({ blob: file }), name: file.name || 'image' });
  };

  // Re-checked wherever the transcript or the attachment tray changes; `busy` still wins, so
  // nothing is cleared mid-turn.
  const syncClearBtn = () => {
    // `:scope >` so the scatter's cloned entries (nested inside .disintegrate-host)
    // don't read as live transcript content and keep Clear enabled after a clear.
    clearBtn.disabled = isBusy()
      || (!transcriptEl.querySelector(':scope > .msg, :scope > .warn, :scope > .card') && !pending.length);
  };

  return { pending, renderTray, addPending, addPendingUrl, addPendingFile, syncClearBtn };
};
