import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { wireModalOpenGestures } from './popover.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { defaultBlankSizePx } from '../core/layout.js';
import { spinIconOnce } from './icons.js';
import { isVideoFile, isVideoUrl, FRAME_INDEX_FPS } from '../core/videoFrame.js';
import { loadMediaCors, retryWithoutCors, canReadPixels } from './mediaCors.js';
import { fetchUrlToFile, toFrameIfVideo as frameIfVideo } from '../core/imageSourceLoader.js';
import { isAlbumOrientation } from '../core/cropGeometry.js';
import { createCropOverlay, freshCropState, hasCropRect } from './openImageCrop.js';
import { makeDustStage } from './motion/canvasDustStage.js';
import { runDust } from './motion/canvasDustDraw.js';
import { GHOST_MS } from './motion/canvasDustGrid.js';
import { dustEnabled } from './motionPrefs.js';
import { modalBoxEase } from './motion/easeBoxHeight.js';
import { makeDustRow, makeDustToggle } from './motion/dustRow.js';
import { openImageModalInner } from './openImageMarkup.js';
const { PAGE_SIZES } = constants;
// Desktop twin: OpenImageDialog's fetchTimer_ interval — one seek per settled drag.
const SEEK_SETTLE_MS = 80;

// The single way to get an image into the editor (Local file / URL link / Blank tabs); the DOM is built once and reused, so onOpen MUST reset every field.
export class StencilOpenImageModal extends StencilElement {
  static inner() { return openImageModalInner(); }
  static template() { return hostTag('stencil-open-image-modal', 'id="open-image-modal-overlay" class="app-modal-overlay"', StencilOpenImageModal.inner()); }

  wire(app) {
    const $ = id => document.getElementById(id);
    const overlay = $('open-image-modal-overlay');
    const closeBtn = $('open-image-close');
    const cancelBtn = $('open-image-cancel');
    const fileEl = $('open-image-file');
    const chooseBtn = $('open-image-choose');
    const fileNameEl = $('open-image-file-name');
    // The hidden input holds the pick; this span is what the reader sees.
    const showFileName = () => {
      const picked = fileEl.files && fileEl.files[0];
      fileNameEl.textContent = picked ? picked.name : 'No file chosen';
      fileNameEl.classList.toggle('is-empty', !picked);
    };
    const urlEl = $('open-image-url');
    const urlPreviewBtn = $('open-image-url-preview');
    const incog = $('open-image-incognito');
    const hereBtn = $('open-image-here');
    const newTabBtn = $('open-image-newtab');
    const replaceBtn = $('open-image-replace');
    const replaceRow = $('open-image-replace-row');
    const renameEl = $('open-image-rename');
    const keepEl = $('open-image-keep');
    const targetEl = $('open-image-target');
    const targetRow = $('open-image-target-row');
    const frameEl = $('open-image-frame'), statusEl = $('open-image-preview-status');
    const frameRow = $('open-image-frame-row');
    const previewWrap = $('open-image-preview');
    // One media pair per source tab (desktop twin: TabPreviewCache): a tab's decode survives
    // the other tab's own, so a switch back re-fetches and re-decodes nothing. `previewImg` /
    // `previewVideo` are the ACTIVE tab's pair; setTab swaps them.
    let previewImg = $('open-image-preview-img');
    let previewVideo = $('open-image-preview-video');
    const media = { file: { img: previewImg, video: previewVideo } };
    media.url = { img: previewImg.cloneNode(false), video: previewVideo.cloneNode(false) };
    for (const el of [media.url.img, media.url.video]) { el.removeAttribute('id'); el.style.display = 'none'; }
    previewImg.after(media.url.video, media.url.img);
    // What each pair holds: the source key being loaded, its kind, the object URL behind
    // it, and a decode that landed while its tab was away (delivered on the way back).
    const freshTab = () => ({ loading: null, isVideo: false, pending: false, objectUrl: null, src: '', pixelsReadable: true });
    const tabState = { file: freshTab(), url: freshTab() };
    const cropRow = $('open-image-crop-row');
    const cropToggle = $('open-image-crop-toggle');
    const cropStage = $('open-image-crop-stage');
    const cropBox = $('open-image-crop-box');
    const cropShade = $('open-image-crop-shade');
    const cropDims = $('open-image-crop-dims');
    const orientBtn = $('open-image-crop-orientation');
    const cropSizeRow = $('open-image-crop-size-row');
    const cropSizeSel = $('open-image-crop-size');
    const cropSizeCustom = $('open-image-crop-size-custom');
    const cropSizeW = $('open-image-crop-size-w'), cropSizeH = $('open-image-crop-size-h');
    const scrubEl = $('open-image-frame-scrub');
    const tabs = [$('oi-tab-file'), $('oi-tab-url'), $('oi-tab-blank')];
    const panels = { file: $('oi-panel-file'), url: $('oi-panel-url'), blank: $('oi-panel-blank') };
    const colorEl = $('blank-image-color'), colorHexEl = $('blank-image-color-hex');
    const widthEl = $('blank-image-width');
    const heightEl = $('blank-image-height');
    const createBtn = $('blank-image-create');

    let activeTab = 'file';
    const tabSt = () => tabState[activeTab] || tabState.file;
    const boxEase = modalBoxEase(overlay);   // the box eases between its content heights

    // Only a saved local or server-linked project — not a blank / incognito session.
    const canReplace = () => !!(app.image && !app.storage.incognito
      && (app.activeProjectId != null || app.remoteLink));

    // The crop's OWN aspect ratio — starts on the project's own page, but picking a
    // different one here (Custom included) affects only this preview, never the project.
    // Plain ratios beside it: every named ISO page (A/B/C) shares one ratio (√2), so
    // offering the whole list said nothing a single "Page" entry doesn't already say.
    const CROP_RATIOS = { '1:1': { width: 1, height: 1 }, '2:3': { width: 2, height: 3 } };
    let cropPageKey = 'page', cropCustomW = 21, cropCustomH = 29.7;
    // NOT getPageDimensions(): that swaps to landscape from the current canvas aspect.
    const pageDims = () => {
      if (cropPageKey === 'custom') return { width: cropCustomW, height: cropCustomH };
      if (CROP_RATIOS[cropPageKey]) return CROP_RATIOS[cropPageKey];
      return app.pageSize === 'custom'
        ? { width: app.customPageWidth, height: app.customPageHeight }
        : (PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);
    };

    const urlVal = () => urlEl.value.trim();
    const chosenFile = () => fileEl.files && fileEl.files[0];
    const hasSource = () => (activeTab === 'file' ? !!chosenFile() : urlVal() !== '');
    const isVideoSource = () => (activeTab === 'url'
      ? isVideoUrl(urlVal())
      : !!chosenFile() && isVideoFile(chosenFile()));
    // A half-typed URL never triggers a fetch: the explicit Preview button is gated on this.
    const isPreviewableUrl = (v) => {
      if (!v) return false;
      try { return /^(https?:|data:|blob:)$/i.test(new URL(v).protocol); } catch { return false; }
    };
    // The field counts FRAMES (desktop's Frame row); every consumer below wants seconds.
    const frameSeconds = () => (Number(frameEl && frameEl.value) || 0) / FRAME_INDEX_FPS;
    const lastFrame = () => Math.max(0, Math.round((previewVideo.duration || 0) * FRAME_INDEX_FPS) - 1);
    const syncFrameBounds = () => { frameEl.max = scrubEl.max = String(lastFrame()); };
    // The bar shows how far in the frame sits, and spans exactly the picture above it.
    const syncScrub = () => {
      const n = Number(frameEl.value) || 0;
      scrubEl.value = String(n);
      const max = Number(scrubEl.max) || 0;
      scrubEl.style.setProperty('--scrub-fill', `${max > 0 ? (n / max) * 100 : 0}%`);
      const w = Math.round(cropMedia().getBoundingClientRect().width);
      if (w) scrubEl.style.width = `${w}px`;
    };
    // A local file previews as soon as it's chosen; a URL only after Preview is pressed.
    const previewReady = () => (activeTab === 'file' ? !!chosenFile() : activeTab === 'url' && urlPreviewLoaded);

    // The crop rect and everything that draws it (openImageCrop.js); this file keeps the
    // tabs and the media the rect is drawn ON.
    const cropState = freshCropState();
    // Whether the preview is live for the typed URL (a URL never previews on keystroke).
    let urlPreviewLoaded = false;
    // …and whether a URL preview is on screen at all: it stays up while the URL is corrected.
    let urlPreviewShown = false, mediaReady = false;   // decoded, so the box has its REAL size

    const revokeObjectUrl = (st) => { if (st.objectUrl) { URL.revokeObjectURL(st.objectUrl); st.objectUrl = null; } };

    // The crop rides whichever media is on show — for a video that is the PLAYER, so the
    // rect is drawn on the picture already there instead of on a second copy of it.
    const cropMedia = () => (tabSt().isVideo ? previewVideo : previewImg);
    const mediaPixels = () => (tabSt().isVideo
      ? { width: previewVideo.videoWidth, height: previewVideo.videoHeight }
      : { width: previewImg.naturalWidth, height: previewImg.naturalHeight });

    const crop = createCropOverlay({
      state: cropState,
      els: { box: cropBox, shade: cropShade, dims: cropDims, orient: orientBtn },
      pageDims, media: cropMedia, onDrag: () => persistCropRect(),
    });

    // Whatever just decoded becomes the crop's ground: its own pixels, its own rect.
    // cropState is GLOBAL (one crop box, whichever tab owns it right now) — any OTHER tab's
    // own decode overwrites it for ITS pixels. Called whenever the box needs to agree with
    // the CURRENT tab's picture: this tab's own saved rect if it was dragged on this exact
    // decode (desktop twin: TabPreviewCache::cropRect), else a fresh default.
    const restoreCropFor = (width, height) => {
      cropState.iw = width; cropState.ih = height;
      const saved = (activeTab === 'file' || activeTab === 'url') && tabCropRect[activeTab];
      if (saved && saved.iw === width && saved.ih === height) {
        cropState.rect = { ...saved.rect };
        cropState.album = isAlbumOrientation(cropState.rect.width, cropState.rect.height);
      } else {
        cropState.album = isAlbumOrientation(width, height);
        cropState.rect = { x: 0, y: 0, width: 0, height: 0 };
      }
    };

    const mediaDecoded = (el, { width, height }) => {
      if (!width || !height) return;
      if (width !== cropState.iw || height !== cropState.ih) restoreCropFor(width, height);
      mediaReady = true; tabSeen[activeTab] = tabSt().loading;
      tabReady[activeTab] = true;
      // Cropping reads the frame back, so it needs pixels the host actually let us read.
      tabSt().pixelsReadable = canReadPixels(el);
      syncPreview(); refresh();
      crop.computeScale();
      gatherPreviewDust();
      if (!cropToggle.checked) return;
      if (cropState.rect.width < 1) crop.recenter();
      else crop.render();
    };
    // Each pair's own events. Only the ACTIVE tab's decode lands now; one arriving while
    // its tab is away waits (pending) and lands when the tab is shown again.
    const wireMedia = (tab) => {
      const st = tabState[tab], { img, video } = media[tab];
      const landed = (el) => { if (tab === activeTab) mediaDecoded(el, mediaPixels()); else st.pending = true; };
      img.addEventListener('load', () => { if (!st.isVideo) landed(img); });
      // An undecoded <video> reports 300x150, so the rect waits for its real frame size.
      video.addEventListener('loadeddata', () => {
        if (!st.isVideo) return;
        if (tab === activeTab) syncFrameBounds();
        landed(video);
      });
      // A host that refuses the CORS ask fails the load — come back for the pixels-less one.
      const failed = (el) => {
        if (retryWithoutCors(el, st.src)) return;
        if (tab === activeTab) statusEl.textContent = 'Could not load that source.';
      };
      img.addEventListener('error', () => { if (!st.isVideo) failed(img); });
      video.addEventListener('error', () => { if (st.isVideo) failed(video); });
      video.addEventListener('seeked', () => {
        // A seek that has landed names the frame on screen — unless a newer one is already
        // on its way, in which case this would rewind the field the user just set.
        if (video !== previewVideo || seekTimer) return;
        frameEl.value = String(Math.round(video.currentTime * FRAME_INDEX_FPS));
        syncScrub();
      });
    };
    wireMedia('file');
    wireMedia('url');

    const cropEnabled = () => cropToggle.checked && previewReady();

    // Every crop-only row (the size read-out, the page-size picker) flies on the KEYWORD-
    // CHIP recipe (motion/dustRow.js) so it reads like one on both surfaces — the desktop
    // twin plays the same numbers. The play belongs to the CHECKBOX alone: a tab switch
    // that merely restores the other tab's crop choice swaps it silently, or every switch
    // replays an arrival.
    let cropByUser = false;
    const syncCropDims = makeDustRow(cropDims);
    // Its own flight, separate from the row's: the row's OWN cloud (below) must stay
    // small and single-line, not the whole two-line block, or disabling Crop while
    // Custom is picked scatters a cloud tall enough to spill onto the Incognito row
    // under it (user report). 'inline-flex' — it wraps onto the row's own second line.
    const syncCropSizeCustom = makeDustRow(cropSizeCustom, 'inline-flex');
    // 'flex', not the factory's default 'block': this row is a .vs-row (label beside its
    // control, like Crop above it) — 'block' let the label and the select stack instead of
    // sitting on one line whenever the control span didn't fit beside the label (user report).
    // Its cloud is scoped to what the row actually holds, not the whole row: the row is
    // however wide the modal is, but the label and its trailing space are not what changed
    // (user report) — the bare selector, or with Custom's W/H the control span they fill.
    const syncCropSizeRow = makeDustRow(cropSizeRow, 'flex',
      () => (cropPageKey === 'custom' ? cropSizeRow.querySelector('.oi-crop-size') : cropSizeSel));
    // orientBtn sits INLINE beside the checkbox/caption in a row those two already keep
    // open — no height of its own to collapse, so the toggle's cloud without the row-slide.
    const syncOrientBtn = makeDustToggle(orientBtn);

    // ghostIn IDENTICAL (motion/canvasFx.js): media hidden while an overlay canvas assembles
    // it in ITS OWN colours. Plays ONCE per source ever seen this session.
    const animatedSources = new Set();
    // A departure ALWAYS has an arrival: the old picture blowing away and the new one
    // simply appearing is the asymmetry this flag closes (desktop twin: arrivalDue_).
    let arrivalDue = false;
    const sourceKey = () => (activeTab === 'file'
      ? (chosenFile() ? `${chosenFile().name}:${chosenFile().size}:${chosenFile().lastModified}` : '')
      : urlVal());
    const willFly = () => {
      const key = sourceKey();
      return !!key && (arrivalDue || !animatedSources.has(key));
    };
    const preVeilIfNew = (el) => { if (willFly()) el.style.opacity = '0'; };
    const gatherPreviewDust = () => {
      if (!willFly()) return;
      animatedSources.add(sourceKey());
      arrivalDue = false;
      const video = tabSt().isVideo;
      const el = video ? previewVideo : previewImg;
      const size = video ? { width: previewVideo.videoWidth, height: previewVideo.videoHeight }
                          : { width: previewImg.naturalWidth, height: previewImg.naturalHeight };
      dustOver(el, size, true);
    };
    let swapTimer = 0;
    // Decoration only: a taint or a zero-sized box must never leave the media veiled.
    let ghostStage = null, ghostTimer = 0;
    const dustOver = (el, size, gather) => {
      if (!dustEnabled() || !size.width || !size.height) { el.style.opacity = ''; return; }
      try {
        const st = makeDustStage(el, size);
        if (!st) { el.style.opacity = ''; return; }
        ghostStage = st;
        runDust(st, GHOST_MS, gather);
        ghostTimer = setTimeout(() => {
          ghostStage = null; ghostTimer = 0; el.style.opacity = '';
        }, GHOST_MS);
      } catch { el.style.opacity = ''; }
    };
    // The stage is a canvas over the media, so the overlay's sweepDust never reaches it:
    // left running, it played over the picture the next tab put there (desktop twin:
    // cancelPreviewDust). The veil it stood in for lifts with it, or the media stays hidden.
    const cancelPreviewDust = () => {
      if (ghostStage) { try { ghostStage.finish(); } catch { /* decoration */ } ghostStage = null; }
      clearTimeout(ghostTimer);
      clearTimeout(swapTimer);
      ghostTimer = swapTimer = 0;
      previewImg.style.opacity = previewVideo.style.opacity = '';
    };
    // Each tab keeps its OWN source and its OWN crop choice: ticking Crop on the URL
    // tab says nothing about the local file, and vice versa.
    const tabSeen = { file: null, url: null }, tabCrop = { file: false, url: false };
    // The tab's OWN last-dragged rect, keyed to the decode it was dragged on — a fresh
    // centeredCrop() otherwise threw every drag away on a tab switch, even a round trip
    // back to the exact same picture (desktop twin: TabPreviewCache::cropRect).
    const tabCropRect = { file: null, url: null };
    const persistCropRect = () => {
      if (activeTab !== 'file' && activeTab !== 'url') return;
      tabCropRect[activeTab] = { rect: { ...cropState.rect }, iw: cropState.iw, ih: cropState.ih };
    };
    // Each tab remembers that IT had a decoded picture: the other tab's load clears the
    // shared `mediaReady`, and coming back must not read that as "nothing to show".
    const tabReady = { file: false, url: false };
    const syncPreview = () => {
      const show = previewReady() && mediaReady;
      // The picture may outlive its liveness (urlPreviewShown) while its URL is corrected —
      // only ON the url tab, or it leaks onto file/blank after a switch.
      const willShow = show || (activeTab === 'url' && urlPreviewShown && mediaReady);
      previewWrap.style.display = willShow ? '' : 'none';
      if (previewReady() && !mediaReady) statusEl.textContent = 'Loading…';
      else if (statusEl.textContent === 'Loading…') statusEl.textContent = '';
      // The crop CHOICE belongs to the picture on screen, not to the typed text: editing a
      // URL leaves the old picture up (willShow), and hiding the row with it dropped a
      // ticked Crop out of sight. Opening re-resolves the typed url and crops that.
      cropRow.style.display = willShow ? '' : 'none';
      cropToggle.disabled = !tabSt().pixelsReadable;
      if (cropToggle.disabled) cropToggle.checked = false;
      const cropUiShown = willShow && cropToggle.checked;
      // Silent, and BEFORE the row's own cloud: collapsing Custom's fields first means the
      // row disintegrates from its plain single-line shape, not the taller box with them
      // still in it — the "too much" cloud the user saw (image #97) was that whole block
      // flying at once, over the Incognito row under it.
      syncCropSizeCustom(cropUiShown && cropPageKey === 'custom', false);
      syncOrientBtn(cropUiShown, cropByUser);
      syncCropSizeRow(cropUiShown, cropByUser);
      const video = tabSt().isVideo;
      // ONE stage, holding whichever media this source is: cropping draws a rect on the
      // picture that is already there — for a video that is the player itself. Nothing
      // appears or leaves, so nothing has an arrival to play (desktop twin: syncCropStage).
      if (willShow) {
        previewVideo.style.display = video ? 'block' : 'none';
        previewImg.style.display = video ? 'none' : 'block';
        cropStage.style.display = '';
      }
      if (!show) { crop.hide(); syncCropDims(false, false); return; }
      const cropping = cropToggle.checked;
      scrubEl.style.display = video ? 'block' : 'none';
      if (video) syncScrub();
      if (!cropping) {
        crop.hide();
      } else if (cropState.iw && cropState.ih) {
        // Loaded before Crop was ticked (no rect fitted yet): fit one now.
        crop.computeScale();
        if (cropState.rect.width < 1) {
          cropState.album = isAlbumOrientation(cropState.iw, cropState.ih);
          crop.recenter();
        } else crop.render();
      }
      syncCropDims(cropping, cropByUser);
      cropByUser = false;
    };

    // A URL loads straight into the element (display never taints, unlike a canvas readback).
    // Always decodes; showTabPreview below skips it when the tab's source is unchanged.
    const loadPreviewMedia = (replacing = false) => {
      // The user SWAPPING the source blows the old picture away first, so the two never
      // cross-fade (desktop twin: scatterPreviewDust). A tab switch swaps nothing — the
      // other tab's picture is simply put back — so it never plays.
      if (replacing && mediaReady) {
        const el = cropMedia();
        const { width: w, height: h } = mediaPixels();
        el.style.opacity = '0';   // the picture goes NOW; its cloud carries it out
        dustOver(el, { width: w, height: h }, false);
        arrivalDue = true;        // …so whatever lands next flies in, seen before or not
        // The OLD crop rect belongs to the OLD pixels: left up, it hovers over a vanished
        // picture and then jumps to the new one's box once that lands — a visible "reset".
        // Cleared instantly (no particle flight of its own) the moment the picture goes.
        crop.hide();
        syncCropDims(false, false);
        syncOrientBtn(false, false);
        syncCropSizeCustom(false, false);
        syncCropSizeRow(false, false);
        // The arrival starts only once the departure has landed: run together, the two
        // clouds share one host and the second wipes the first off it.
        clearTimeout(swapTimer);
        swapTimer = setTimeout(() => { swapTimer = 0; loadPreviewMedia(false); }, GHOST_MS);
        return;
      }
      // Nothing to load on THIS tab (Blank, an unpreviewed URL): its pair keeps what it has.
      if (!previewReady()) { mediaReady = false; syncPreview(); return; }
      const st = tabSt(), pair = media[activeTab], key = sourceKey();
      // The pair already holds this very source: nothing to fetch, nothing to re-decode.
      if (st.loading === key && tabSeen[activeTab] === key) { syncPreview(); return; }
      revokeObjectUrl(st);
      cropState.iw = cropState.ih = 0;
      mediaReady = false;
      crop.hide();
      syncCropDims(false, false);
      syncOrientBtn(false, false);
      syncCropSizeCustom(false, false);
      syncCropSizeRow(false, false);
      st.loading = key;
      st.pending = false;
      st.isVideo = isVideoSource();
      const file = activeTab === 'file' ? chosenFile() : null;
      st.src = file ? (st.objectUrl = URL.createObjectURL(file)) : urlVal();
      preVeilIfNew(st.isVideo ? pair.video : pair.img);
      if (st.isVideo && !file) {
        // A REMOTE video answers every seek with its own range request — about a second per
        // frame off a CDN. Desktop downloads the clip once and scrubs the local copy
        // (MediaLoader), so this does the same: one fetch, then every seek is local. A
        // blocked fetch falls back to streaming the URL itself, slow scrub and all.
        pair.img.removeAttribute('src');
        fetchUrlToFile(st.src)
          .then((f) => {
            if (st.loading !== key) return;   // the user moved on while it downloaded
            st.objectUrl = URL.createObjectURL(f);
            loadMediaCors(pair.video, st.objectUrl, true);
          })
          .catch(() => { if (st.loading === key) loadMediaCors(pair.video, st.src, false); });
        syncPreview();
        return;
      }
      if (st.isVideo) {
        loadMediaCors(pair.video, st.src, !!file);
        pair.img.removeAttribute('src');
      } else {
        loadMediaCors(pair.img, st.src, !!file);
        pair.video.pause();
        pair.video.removeAttribute('src');
      }
      syncPreview();
    };
    // cropState is ONE crop box, whichever tab owns it: a tab re-shown without a fresh
    // decode must first bring it back to its own picture, or the other tab's rect is
    // drawn mis-scaled over this one.
    const reconcileCropWithLiveMedia = () => {
      const px = mediaPixels();
      if (px.width && px.height && (px.width !== cropState.iw || px.height !== cropState.ih)) {
        restoreCropFor(px.width, px.height);
      }
    };
    const showTabPreview = () => {
      const st = tabSt(), key = sourceKey();
      // Decoded while this tab was away: it lands now, exactly as if it had just arrived.
      if (st.pending) {
        st.pending = false;
        if (st.isVideo) syncFrameBounds();
        mediaDecoded(cropMedia(), mediaPixels());
        return;
      }
      if (key && key === st.loading) {
        if (st.isVideo) syncFrameBounds();
        reconcileCropWithLiveMedia();
        syncPreview();
        return;
      }
      // The URL text moved on while this tab was away: coming back still shows what it was
      // showing — losing the picture to a half-typed address is not a tab switch's doing,
      // and Preview is what replaces it (desktop twin: applyMode's stale restore).
      if (activeTab === 'url' && urlPreviewShown && tabReady.url && st.loading === tabSeen.url) {
        mediaReady = true;
        reconcileCropWithLiveMedia();
        syncPreview();
        return;
      }
      loadPreviewMedia();
    };

    // Blank tab has its own Create button and no source concept.
    const refresh = () => {
      urlPreviewBtn.disabled = !isPreviewableUrl(urlVal());
      if (activeTab === 'blank') { frameRow.style.display = 'none'; return; }
      const has = hasSource();
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
      replaceBtn.disabled = !has || activeTab === 'url' || isVideoSource() || !canReplace();
      // A URL video has no scrubber until Preview.
      frameRow.style.display = (isVideoSource() && previewReady() && mediaReady) ? '' : 'none';
    };

    // Crop on + measured → the chosen rect, else `noCrop`; a URL carries its own URL too.
    const openOpts = () => {
      const o = (cropEnabled() && hasCropRect(cropState))
        ? { crop: { ...cropState.rect } } : { noCrop: true };
      if (activeTab === 'url') o.source = urlVal();
      return o;
    };

    const setTab = (name) => {
      if (activeTab !== 'blank') tabReady[activeTab] = mediaReady;
      activeTab = name;
      if (name !== 'blank') mediaReady = tabReady[name];
      tabs.forEach(t => t.classList.toggle('is-active', t.dataset.tab === name));
      for (const [k, el] of Object.entries(panels)) el.style.display = k === name ? '' : 'none';
      const blank = name === 'blank';
      hereBtn.style.display = blank ? 'none' : '';
      newTabBtn.style.display = blank ? 'none' : '';
      createBtn.style.display = blank ? '' : 'none';
      // Incognito has no effect on blank creation.
      $('open-image-incognito-row').style.display = blank ? 'none' : '';
      cancelPreviewDust();   // the outgoing tab's flourish does not play over the arriving one
      if (!blank) {
        previewImg.style.display = previewVideo.style.display = 'none';   // the outgoing pair
        ({ img: previewImg, video: previewVideo } = media[name]);
      }
      const showReplace = name === 'file' && canReplace();
      replaceRow.style.display = showReplace ? '' : 'none';
      replaceBtn.style.display = showReplace ? '' : 'none';
      if (!blank) cropToggle.checked = tabCrop[name];
      // A loaded source rides out a tab switch — only onOpen's full reset clears it.
      refresh();
      showTabPreview();
      // The tab's own field takes the caret, so the next keystroke goes where the tab is
      // for (desktop twin: applyMode's setFocus). After the panel is shown, or focusing an
      // element that is still display:none does nothing.
      const focusEl = name === 'file' ? chooseBtn : (name === 'url' ? urlEl : null);
      if (focusEl) requestAnimationFrame(() => { try { focusEl.focus(); } catch { /* no-op */ } });
    };

    const { open, close, openPopover } = wireModalShell(overlay, $('load-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        showFileName();
        urlEl.value = '';
        if (frameEl) frameEl.value = '0';
        // Full reset, so a prior open's image never leaks in.
        urlPreviewLoaded = urlPreviewShown = mediaReady = false;
        tabSeen.file = tabSeen.url = null;
        tabReady.file = tabReady.url = false;
        tabCropRect.file = tabCropRect.url = null;
        tabCrop.file = tabCrop.url = false;
        animatedSources.clear();
        arrivalDue = false;
        for (const t of ['file', 'url']) {
          revokeObjectUrl(tabState[t]);
          Object.assign(tabState[t], freshTab());
          media[t].img.removeAttribute('src');
          media[t].img.style.opacity = media[t].video.style.opacity = '';
          media[t].video.pause();
          media[t].video.removeAttribute('src');
        }
        cropToggle.checked = false;
        Object.assign(cropState, freshCropState());
        // Starts on "Page" — the read below (defaultBlankSizePx) still wants the project's
        // own page, not whatever ratio the crop selector was left on from a prior open.
        cropPageKey = 'page';
        cropCustomW = app.customPageWidth; cropCustomH = app.customPageHeight;
        cropSizeSel.value = cropPageKey;
        cropSizeW.value = cropCustomW; cropSizeH.value = cropCustomH;
        previewWrap.style.display = 'none';
        cropRow.style.display = 'none';
        syncCropDims(false, false);
        syncOrientBtn(false, false);
        syncCropSizeCustom(false, false);
        syncCropSizeRow(false, false);
        incog.checked = false;
        colorEl.value = '#ffffff';
        syncColorHex();
        const px = defaultBlankSizePx(pageDims());
        widthEl.value = px.width;
        heightEl.value = px.height;
        renameEl.checked = false;
        keepEl.checked = true;
        hereBtn.disabled = true;
        newTabBtn.disabled = true;
        replaceBtn.disabled = true;
        // Incognito content isn't created on a server.
        fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
        setTab('file');
        boxEase.start();
      },
      onClose: () => {
        cancelPreviewDust();
        for (const t of ['file', 'url']) { revokeObjectUrl(tabState[t]); media[t].video.pause(); }
        boxEase.stop();
      }
    });
    // `from` = the control that asked, so the dialog grows out of that, not the toolbar icon.
    const openBlank = (from) => { open(from); setTab('blank'); };

    cancelBtn.addEventListener('click', close);
    // The open-ANOTHER icon answers the same gestures too (#load-image-btn hides once an image exists).
    const anotherBtn = $('open-image-btn');
    if (anotherBtn) wireModalOpenGestures(anotherBtn, { openFull: () => open(anotherBtn), openPopover: () => openPopover(anotherBtn) });
    $('create-blank-btn')?.addEventListener('click', () => openBlank($('create-blank-btn')));
    // Close the projects modal via its own close, so its handlers run.
    $('projects-blank-image')?.addEventListener('click', () => {
      $('projects-close')?.click();
      openBlank($('projects-blank-image'));
    });

    tabs.forEach(t => t.addEventListener('click', () => setTab(t.dataset.tab)));

    // Incognito and a server target are mutually exclusive.
    incog.addEventListener('change', () => {
      fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
    });

    // The whole box opens the picker, not just the button — the readout is part of one
    // control (desktop twin: clickActivates on the dialog's path field). Bound to the
    // NAME, not the box, or a click on the button would open the picker twice.
    for (const el of [chooseBtn, fileNameEl]) el.addEventListener('click', () => fileEl.click());
    fileEl.addEventListener('change', () => {
      showFileName();
      refresh();
      loadPreviewMedia(/*replacing=*/true);
    });
    // Editing the URL retires the preview (no crop/scrubber) without taking it off the screen.
    urlEl.addEventListener('input', () => { urlPreviewLoaded = false; refresh(); syncPreview(); });
    const doUrlPreview = () => {
      if (!isPreviewableUrl(urlVal())) { notify('Enter a valid image or video URL (http/https or data:).', 'fail'); return; }
      urlPreviewLoaded = urlPreviewShown = true;
      refresh();
      loadPreviewMedia(/*replacing=*/true);
    };
    urlPreviewBtn.addEventListener('click', doUrlPreview);
    urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); doUrlPreview(); } });

    cropToggle.addEventListener('change', () => {
      if (activeTab !== 'blank') tabCrop[activeTab] = cropToggle.checked;
      cropByUser = true;   // THIS is the change the read-out's play belongs to
      syncPreview();
    });
    // spun AFTER the repaint: crop.recenter() rewrites the glyph, dropping a running turn.
    orientBtn.addEventListener('click', () => {
      cropState.album = !cropState.album;
      crop.recenter(true); persistCropRect(); spinIconOnce(orientBtn);
    });

    // A different page picked for the crop: unlike the orientation flip, an arbitrary new
    // aspect has no reciprocal to carry the old box across — same fresh default a first
    // Crop tick gets (browser twin of desktop's own page-size change).
    const applyCropPageChange = () => { if (crop.fitToPage()) persistCropRect(); };
    cropSizeSel.addEventListener('change', () => {
      cropPageKey = cropSizeSel.value;
      syncCropSizeCustom(cropPageKey === 'custom', true);
      applyCropPageChange();
    });
    cropSizeW.addEventListener('input', () => {
      cropCustomW = parseFloat(cropSizeW.value) || cropCustomW;
      if (cropPageKey === 'custom') applyCropPageChange();
    });
    cropSizeH.addEventListener('input', () => {
      cropCustomH = parseFloat(cropSizeH.value) || cropCustomH;
      if (cropPageKey === 'custom') applyCropPageChange();
    });

    // When cropping, a settled seek re-captures the still so the crop tracks the frame.
    // ONE seek path for the field and the bar, so the two can never disagree and neither
    // needs to fake an event at the other. Dragging fires per pixel, so the seek waits for
    // the drag to settle, exactly as the desktop's does (OpenImageDialog fetchTimer_).
    let seekTimer = 0;
    const seekToFrame = (n) => {
      frameEl.value = String(n);
      syncScrub();
      if (!isVideoSource() || !previewVideo.readyState) return;
      clearTimeout(seekTimer);
      seekTimer = setTimeout(() => {
        seekTimer = 0;
        try { previewVideo.currentTime = n / FRAME_INDEX_FPS; } catch { /* out-of-range seek */ }
      }, SEEK_SETTLE_MS);
    };
    frameEl.addEventListener('input', () => seekToFrame(Number(frameEl.value) || 0));
    scrubEl.addEventListener('input', () => seekToFrame(Number(scrubEl.value) || 0));

    // Every route that writes the fill writes the hex with it: the presets set the SAME
    // value the picker holds (desktop parity — both land on customColor_).
    const syncColorHex = () => {
      if (colorHexEl) colorHexEl.textContent = String(colorEl.value || '').toUpperCase();
    };
    colorEl.addEventListener('input', syncColorHex);
    $('blank-image-white').addEventListener('click', () => { colorEl.value = '#ffffff'; syncColorHex(); });
    $('blank-image-black').addEventListener('click', () => { colorEl.value = '#000000'; syncColorHex(); });

    const toFrameIfVideo = (file) => frameIfVideo(file, frameSeconds());

    // The active tab's source as a still-image File, or null on error (already notified).
    const resolveSource = async () => {
      try {
        const file = activeTab === 'url' ? await fetchUrlToFile(urlVal()) : chosenFile();
        if (!file) return null;
        return await toFrameIfVideo(file);
      } catch (e) {
        notify(activeTab === 'url'
          ? `Could not load that URL — ${e.message}. Cross-origin URLs need CORS headers; try the extension or desktop app.`
          : `Could not capture a video frame — ${e.message}`, 'fail');
        return null;
      }
    };

    hereBtn.addEventListener('click', async () => {
      if (!hasSource()) return;
      const address = (targetEl && targetEl.value) || null;
      const opts = openOpts();
      const resolved = await resolveSource();
      if (!resolved) return;
      app.openImageHere(resolved, incog.checked, address, opts);
      close();
    });
    newTabBtn.addEventListener('click', async () => {
      if (!hasSource()) return;
      const opts = openOpts();
      const resolved = await resolveSource();
      if (!resolved) return;
      app.openImageNewTab(resolved, incog.checked, opts);
      close();
    });
    replaceBtn.addEventListener('click', async () => {
      if (activeTab !== 'file' || !chosenFile() || !canReplace()) return;
      const resolved = await resolveSource();
      if (!resolved) return;
      app.replaceProjectImage(resolved, { rename: renameEl.checked, keepAnnotations: keepEl.checked });
      close();
    });

    createBtn.addEventListener('click', async () => {
      const w = parseInt(widthEl.value, 10), h = parseInt(heightEl.value, 10);
      if (!(w >= 1 && w <= 8192) || !(h >= 1 && h <= 8192)) {
        notify('Width and height must be 1–8192 px', 'fail');
        return;
      }
      if (app.image && !(await app.confirm('Replace the current image with a new blank image?', { title: 'Replace image', confirmIcon: 'swap' }))) return;
      const address = (targetEl && targetEl.value) || undefined;
      app.createBlankImage({ color: colorEl.value, width: w, height: h, address })
        .then(() => { close(); notify(`Blank ${w}×${h} image created`, 'ok'); })
        .catch((err) => notify(err && err.message ? err.message : 'Could not create the image', 'fail'));
    });
  }
}
define('stencil-open-image-modal', StencilOpenImageModal);
