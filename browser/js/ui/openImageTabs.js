// The Open-Image dialog's three source tabs: which one is active, what each remembers of its
// own source and crop, and what a switch shows. Composes the media pairs and the preview flow.
import { isAlbumOrientation } from '../core/cropGeometry.js';
import { createMediaPairs } from './openImageMediaPairs.js';
import { createPreviewFlow } from './openImagePreviewFlow.js';

export function createOpenImageTabs({ els, src, cropState, crop, cropRows, dust, frame, refresh, canReplace }) {
  const { previewWrap, cropRow, cropToggle, tabBtns, panels, incogRow,
    hereBtn, newTabBtn, createBtn, replaceRow, replaceBtn, chooseBtn, urlEl } = els;

  let activeTab = 'file';
  const tab = () => activeTab;
  const isVideoSource = () => src.isVideoOn(activeTab);
  const sourceKey = () => src.keyOn(activeTab);
  // A local file previews as soon as it's chosen; a URL only after Preview is pressed.
  const previewReady = () => (activeTab === 'file' ? !!src.chosenFile() : activeTab === 'url' && urlPreviewLoaded);
  // Whether the preview is live for the typed URL (a URL never previews on keystroke).
  let urlPreviewLoaded = false;
  // …and whether a URL preview is on screen at all: it stays up while the URL is corrected.
  let urlPreviewShown = false, mediaReady = false;   // decoded, so the box has its REAL size

  // Each tab keeps its OWN source, decode and crop choice. `cropRect` is keyed to the decode it
  // was dragged on — otherwise a round trip back to the same picture threw the drag away.
  const memo = { seen: { file: null, url: null }, ready: { file: false, url: false },
    crop: { file: false, url: false }, cropRect: { file: null, url: null } };
  const persistCropRect = () => {
    if (activeTab !== 'file' && activeTab !== 'url') return;
    memo.cropRect[activeTab] = { rect: { ...cropState.rect }, iw: cropState.iw, ih: cropState.ih };
  };
  // cropState is GLOBAL — one crop box, whichever tab owns it right now. Uses this tab's saved
  // rect if it was dragged on this exact decode.
  const restoreCropFor = (width, height) => {
    cropState.iw = width; cropState.ih = height;
    const saved = (activeTab === 'file' || activeTab === 'url') && memo.cropRect[activeTab];
    if (saved && saved.iw === width && saved.ih === height) {
      cropState.rect = { ...saved.rect };
      cropState.album = isAlbumOrientation(cropState.rect.width, cropState.rect.height);
    } else {
      cropState.album = isAlbumOrientation(width, height);
      cropState.rect = { x: 0, y: 0, width: 0, height: 0 };
    }
  };

  const pairs = createMediaPairs({
    previewImg: els.previewImg, previewVideo: els.previewVideo, statusEl: els.statusEl, tab,
    onDecode: (el, px) => flow.mediaDecoded(el, px),
    onFrameBounds: () => frame.syncFrameBounds(),
    onSeeked: (video) => frame.showLandedFrame(video),
  });
  const flow = createPreviewFlow({
    els, src, cropState, crop, cropRows, dust, frame, pairs, memo, refresh,
    tab, ready: () => mediaReady, setReady: (v) => { mediaReady = v; },
    previewReady, isVideoSource, sourceKey,
    urlPreviewShown: () => urlPreviewShown, restoreCropFor,
  });

  const setTab = (name) => {
    if (activeTab !== 'blank') memo.ready[activeTab] = mediaReady;
    activeTab = name;
    if (name !== 'blank') mediaReady = memo.ready[name];
    tabBtns.forEach(t => t.classList.toggle('is-active', t.dataset.tab === name));
    for (const [k, el] of Object.entries(panels)) el.style.display = k === name ? '' : 'none';
    const blank = name === 'blank';
    hereBtn.style.display = blank ? 'none' : '';
    newTabBtn.style.display = blank ? 'none' : '';
    createBtn.style.display = blank ? '' : 'none';
    // Incognito has no effect on blank creation.
    incogRow.style.display = blank ? 'none' : '';
    dust.cancel();   // the outgoing tab's flourish does not play over the arriving one
    if (!blank) pairs.swapTo(name);
    const showReplace = name === 'file' && canReplace();
    replaceRow.style.display = showReplace ? '' : 'none';
    replaceBtn.style.display = showReplace ? '' : 'none';
    if (!blank) cropToggle.checked = memo.crop[name];
    // A loaded source rides out a tab switch — only onOpen's full reset clears it.
    refresh();
    flow.showTabPreview();
    // The tab's own field takes the caret (desktop twin: applyMode's setFocus). After the panel is
    // shown — focusing a still-display:none element does nothing.
    const focusEl = name === 'file' ? chooseBtn : (name === 'url' ? urlEl : null);
    if (focusEl) requestAnimationFrame(() => { try { focusEl.focus(); } catch { /* no-op */ } });
  };

  const reset = () => {
    urlPreviewLoaded = urlPreviewShown = mediaReady = false;
    memo.seen.file = memo.seen.url = null;
    memo.ready.file = memo.ready.url = false;
    memo.cropRect.file = memo.cropRect.url = null;
    memo.crop.file = memo.crop.url = false;
    dust.reset();
    pairs.resetAll();
    cropToggle.checked = false;
    previewWrap.style.display = 'none';
    cropRow.style.display = 'none';
  };
  const closeUp = () => { dust.cancel(); pairs.pauseAll(); };

  return {
    tab, ready: () => mediaReady, previewReady, isVideoSource, sourceKey,
    isVideoTab: () => pairs.st().isVideo, img: pairs.img, video: pairs.video,
    cropMedia: pairs.cropMedia, mediaPixels: pairs.mediaPixels,
    persistCropRect, syncPreview: flow.syncPreview, loadPreviewMedia: flow.loadPreviewMedia,
    setTab, reset, closeUp,
    rememberCropChoice: () => { if (activeTab !== 'blank') memo.crop[activeTab] = cropToggle.checked; },
    markUrlPreviewed: () => { urlPreviewLoaded = urlPreviewShown = true; },
    retireUrlPreview: () => { urlPreviewLoaded = false; },
  };
}
