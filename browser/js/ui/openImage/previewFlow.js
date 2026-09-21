// What the Open-Image preview shows and when it decodes: the arriving media, the visibility
// sync and the load path. Part of ui/openImageTabs.js, which owns the tabs and their memory.
import { loadMediaCors, canReadPixels } from '../mediaCors.js';
import { fetchUrlToFile } from '../../core/image/imageSourceLoader.js';
import { isAlbumOrientation } from '../../core/cropGeometry.js';

export function createPreviewFlow(ctx) {
  const { els, src, cropState, crop, cropRows, dust, frame, pairs, memo, refresh,
    tab, ready, setReady, previewReady, isVideoSource, sourceKey, urlPreviewShown, restoreCropFor } = ctx;
  const { previewWrap, statusEl, cropStage, cropRow, cropToggle, scrubEl } = els;

  const mediaDecoded = (el, { width, height }) => {
    if (!width || !height) return;
    if (width !== cropState.iw || height !== cropState.ih) restoreCropFor(width, height);
    setReady(true); memo.seen[tab()] = pairs.st().loading;
    memo.ready[tab()] = true;
    // Cropping reads the frame back, so it needs pixels the host actually let us read.
    pairs.st().pixelsReadable = canReadPixels(el);
    syncPreview(); refresh();
    crop.computeScale();
    dust.gather();
    if (!cropToggle.checked) return;
    if (cropState.rect.width < 1) crop.recenter();
    else crop.render();
  };

  const syncPreview = () => {
    const show = previewReady() && ready();
    // The picture may outlive its liveness (urlPreviewShown) while its URL is corrected —
    // only ON the url tab, or it leaks onto file/blank after a switch.
    const willShow = show || (tab() === 'url' && urlPreviewShown() && ready());
    previewWrap.style.display = willShow ? '' : 'none';
    if (previewReady() && !ready()) statusEl.textContent = 'Loading…';
    else if (statusEl.textContent === 'Loading…') statusEl.textContent = '';
    // The crop CHOICE belongs to the picture on screen, not the typed text: editing a URL leaves
    // the old picture up (willShow), and opening re-resolves the typed url and crops that.
    cropRow.style.display = willShow ? '' : 'none';
    cropToggle.disabled = !pairs.st().pixelsReadable;
    if (cropToggle.disabled) cropToggle.checked = false;
    cropRows.syncCropUi(willShow && cropToggle.checked);
    const video = pairs.st().isVideo;
    // ONE stage, holding whichever media this source is: nothing appears or leaves, so nothing has
    // an arrival to play (desktop twin: syncCropStage).
    if (willShow) {
      pairs.video().style.display = video ? 'block' : 'none';
      pairs.img().style.display = video ? 'none' : 'block';
      cropStage.style.display = '';
    }
    if (!show) { crop.hide(); cropRows.hideDims(); return; }
    const cropping = cropToggle.checked;
    scrubEl.style.display = video ? 'block' : 'none';
    if (video) frame.syncScrub();
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
    cropRows.syncDimsRow(cropping);
  };

  // A URL loads straight into the element (display never taints, unlike a canvas readback).
  // Always decodes; showTabPreview below skips it when the tab's source is unchanged.
  const loadPreviewMedia = (replacing = false) => {
    // Swapping the SOURCE blows the old picture away first, so the two never cross-fade (desktop
    // twin: scatterPreviewDust). A tab switch swaps nothing, so it never plays.
    if (replacing && ready()) {
      dust.scatter(pairs.cropMedia(), pairs.mediaPixels());
      // The OLD crop rect belongs to the OLD pixels, so it is cleared instantly — no flight of
      // its own — the moment the picture goes.
      crop.hide();
      cropRows.hideAll();
      dust.queueArrival(() => loadPreviewMedia(false));
      return;
    }
    // Nothing to load on THIS tab (Blank, an unpreviewed URL): its pair keeps what it has.
    if (!previewReady()) { setReady(false); syncPreview(); return; }
    const st = pairs.st(), pair = pairs.pairFor(tab()), key = sourceKey();
    // The pair already holds this very source: nothing to fetch, nothing to re-decode.
    if (st.loading === key && memo.seen[tab()] === key) { syncPreview(); return; }
    pairs.revoke(st);
    cropState.iw = cropState.ih = 0;
    setReady(false);
    crop.hide();
    cropRows.hideAll();
    st.loading = key;
    st.pending = false;
    st.isVideo = isVideoSource();
    const file = tab() === 'file' ? src.chosenFile() : null;
    st.src = file ? (st.objectUrl = URL.createObjectURL(file)) : src.urlVal();
    dust.preVeilIfNew(st.isVideo ? pair.video : pair.img);
    if (st.isVideo && !file) {
      // A REMOTE video answers every seek with a range request, about a second per frame off a CDN,
      // so the clip is fetched once and scrubbed locally, as the desktop's MediaLoader does.
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

  // cropState is ONE crop box, whichever tab owns it: a tab re-shown without a fresh decode must
  // first bring it back to its own picture, or the other tab's rect is drawn mis-scaled.
  const reconcileCropWithLiveMedia = () => {
    const px = pairs.mediaPixels();
    if (px.width && px.height && (px.width !== cropState.iw || px.height !== cropState.ih)) {
      restoreCropFor(px.width, px.height);
    }
  };
  const showTabPreview = () => {
    const st = pairs.st(), key = sourceKey();
    // Decoded while this tab was away: it lands now, exactly as if it had just arrived.
    if (st.pending) {
      st.pending = false;
      if (st.isVideo) frame.syncFrameBounds();
      mediaDecoded(pairs.cropMedia(), pairs.mediaPixels());
      return;
    }
    if (key && key === st.loading) {
      if (st.isVideo) frame.syncFrameBounds();
      reconcileCropWithLiveMedia();
      syncPreview();
      return;
    }
    // The URL text moved on while this tab was away: coming back still shows what it was showing
    // (desktop twin: applyMode's stale restore).
    if (tab() === 'url' && urlPreviewShown() && memo.ready.url && st.loading === memo.seen.url) {
      setReady(true);
      reconcileCropWithLiveMedia();
      syncPreview();
      return;
    }
    loadPreviewMedia();
  };

  return { mediaDecoded, syncPreview, loadPreviewMedia, showTabPreview };
}
