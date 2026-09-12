import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { wireModalOpenGestures } from './popover.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { defaultBlankSizePx } from '../core/layout.js';
import { icon } from './icons.js';
import { isVideoFile, isVideoUrl, videoFrameDataUrl } from '../core/videoFrame.js';
import { fetchUrlToFile, toFrameIfVideo as frameIfVideo } from '../core/imageSourceLoader.js';
import { cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped, isAlbumOrientation } from '../core/cropGeometry.js';
import { openImageModalInner } from './openImageMarkup.js';
const { PAGE_SIZES } = constants;

// The single way to get an image into the editor: Local file / URL link / Blank tabs. The
// DOM is built once and reused, so onOpen MUST reset every field.
export class StencilOpenImageModal extends StencilElement {
  static inner() { return openImageModalInner(); }
  static template() { return hostTag('stencil-open-image-modal', 'id="open-image-modal-overlay" class="app-modal-overlay"', StencilOpenImageModal.inner()); }

  wire(app) {
    const $ = id => document.getElementById(id);
    const overlay = $('open-image-modal-overlay');
    const closeBtn = $('open-image-close');
    const cancelBtn = $('open-image-cancel');
    const fileEl = $('open-image-file');
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
    const frameEl = $('open-image-frame');
    const frameRow = $('open-image-frame-row');
    const previewWrap = $('open-image-preview');
    const previewImg = $('open-image-preview-img');
    const previewVideo = $('open-image-preview-video');
    const cropRow = $('open-image-crop-row');
    const cropToggle = $('open-image-crop-toggle');
    const cropStage = $('open-image-crop-stage');
    const cropBox = $('open-image-crop-box');
    const cropShade = $('open-image-crop-shade');
    const cropDims = $('open-image-crop-dims');
    const orientBtn = $('open-image-crop-orientation');
    const tabs = [$('oi-tab-file'), $('oi-tab-url'), $('oi-tab-blank')];
    const panels = { file: $('oi-panel-file'), url: $('oi-panel-url'), blank: $('oi-panel-blank') };
    const colorEl = $('blank-image-color');
    const widthEl = $('blank-image-width');
    const heightEl = $('blank-image-height');
    const createBtn = $('blank-image-create');

    let activeTab = 'file';

    // Only a saved local or server-linked project — not a blank / incognito session.
    const canReplace = () => !!(app.image && !app.storage.incognito
      && (app.activeProjectId != null || app.remoteLink));

    // NOT getPageDimensions(): that swaps to landscape from the current canvas aspect.
    const pageDims = () => (app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);

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
    // A local file previews as soon as it's chosen; a URL only after Preview is pressed.
    const previewReady = () => (activeTab === 'file' ? !!chosenFile() : activeTab === 'url' && urlPreviewLoaded);

    // All rect math in original-image pixels (the natural pixels of the imported still).
    let previewObjectUrl = null;
    let cropRect = { x: 0, y: 0, width: 0, height: 0 };
    let cropAlbum = false;
    let cropAspectV = 1;
    let cropScale = 1;
    let cropIw = 0, cropIh = 0;
    // Whether the preview is live for the typed URL (a URL never previews on keystroke).
    let urlPreviewLoaded = false;
    // …and whether a URL preview is on screen at all: it stays up while the URL is corrected.
    let urlPreviewShown = false;

    const revokePreviewUrl = () => { if (previewObjectUrl) { URL.revokeObjectURL(previewObjectUrl); previewObjectUrl = null; } };

    const renderCropBox = () => {
      cropBox.style.display = 'block';
      cropBox.style.left = (cropRect.x * cropScale) + 'px';
      cropBox.style.top = (cropRect.y * cropScale) + 'px';
      cropBox.style.width = (cropRect.width * cropScale) + 'px';
      cropBox.style.height = (cropRect.height * cropScale) + 'px';
      cropShade.style.display = 'block';
      cropShade.style.left = cropBox.style.left;
      cropShade.style.top = cropBox.style.top;
      cropShade.style.width = cropBox.style.width;
      cropShade.style.height = cropBox.style.height;
      cropDims.style.display = 'block';
      cropDims.textContent = `${Math.round(cropRect.width)} × ${Math.round(cropRect.height)} px · ${cropAlbum ? 'Album (landscape)' : 'Portrait'}`;
      orientBtn.innerHTML = icon('swap', { size: 14 }) + `<span>${cropAlbum ? 'Album' : 'Portrait'}</span>`;
    };

    const computeCropScale = () => {
      const r = previewImg.getBoundingClientRect();
      cropScale = cropIw > 0 && r.width > 0 ? r.width / cropIw : 1;
    };

    const recenterCrop = () => {
      cropAspectV = cropAspect(pageDims().width, pageDims().height, cropAlbum);
      cropRect = centeredCrop(cropIw, cropIh, cropAspectV);
      renderCropBox();
    };

    // Re-fit the crop when the geometry is new; every frame of one video shares its dimensions.
    previewImg.addEventListener('load', () => {
      const nw = previewImg.naturalWidth, nh = previewImg.naturalHeight;
      if (!nw || !nh) return;
      const geometryChanged = nw !== cropIw || nh !== cropIh;
      cropIw = nw; cropIh = nh;
      computeCropScale();
      if (!cropToggle.checked) return;
      if (geometryChanged) { cropAlbum = isAlbumOrientation(cropIw, cropIh); recenterCrop(); }
      else renderCropBox();
    });

    const cropEnabled = () => cropToggle.checked && previewReady();

    // The shared frame extractor, so the cropped pixels match what import captures.
    const captureVideoFrameForCrop = () => {
      const src = activeTab === 'file' ? URL.createObjectURL(chosenFile()) : urlVal();
      videoFrameDataUrl(src, Number(frameEl && frameEl.value) || 0)
        .then(dataUrl => { previewImg.src = dataUrl; })
        .catch(e => {
          notify(`Could not read that video frame for cropping — ${e.message}`, 'fail');
          cropToggle.checked = false;
          syncPreview();
        });
    };

    const hideCropOverlay = () => {
      cropBox.style.display = 'none';
      cropShade.style.display = 'none';
      cropDims.style.display = 'none';
    };

    const syncPreview = () => {
      const show = previewReady();
      // The picture may outlive its liveness (urlPreviewShown); what ACTS on the source follows `show`.
      previewWrap.style.display = (show || urlPreviewShown) ? '' : 'none';
      cropRow.style.display = show ? '' : 'none';
      orientBtn.style.display = show && cropToggle.checked ? '' : 'none';
      if (!show) { hideCropOverlay(); return; }
      const video = isVideoSource();
      const cropping = cropToggle.checked;
      // The crop stage shows for images always, for a video only while cropping.
      previewVideo.style.display = video ? '' : 'none';
      cropStage.style.display = (!video || cropping) ? '' : 'none';
      if (!cropping) {
        hideCropOverlay();
      } else if (cropIw && cropIh && !video && previewImg.complete && previewImg.naturalWidth) {
        // Loaded before Crop was ticked (no rect fitted yet): fit one now.
        computeCropScale();
        if (cropRect.width < 1) { cropAlbum = isAlbumOrientation(cropIw, cropIh); recenterCrop(); }
        else renderCropBox();
      }
      if (video && cropping) captureVideoFrameForCrop();
    };

    // A URL loads straight into the element (display never taints, unlike a canvas readback).
    const loadPreviewMedia = () => {
      revokePreviewUrl();
      cropIw = cropIh = 0;
      if (!previewReady()) { syncPreview(); return; }
      const video = isVideoSource();
      const file = activeTab === 'file' ? chosenFile() : null;
      const src = file ? (previewObjectUrl = URL.createObjectURL(file)) : urlVal();
      if (video) {
        previewVideo.src = src;
        previewImg.removeAttribute('src');
      } else {
        previewImg.src = src;
        previewVideo.pause();
        previewVideo.removeAttribute('src');
      }
      syncPreview();
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
      frameRow.style.display = (isVideoSource() && previewReady()) ? '' : 'none';
    };

    // Crop on + measured → the chosen rect; otherwise `noCrop` so the whole frame imports.
    // A URL source carries its own URL as provenance (the extension's resume-by-source).
    const openOpts = () => {
      const o = (cropEnabled() && cropRect.width >= 1 && cropRect.height >= 1)
        ? { crop: { ...cropRect } } : { noCrop: true };
      if (activeTab === 'url') o.source = urlVal();
      return o;
    };

    const setTab = (name) => {
      activeTab = name;
      tabs.forEach(t => t.classList.toggle('is-active', t.dataset.tab === name));
      for (const [k, el] of Object.entries(panels)) el.style.display = k === name ? '' : 'none';
      const blank = name === 'blank';
      hereBtn.style.display = blank ? 'none' : '';
      newTabBtn.style.display = blank ? 'none' : '';
      createBtn.style.display = blank ? '' : 'none';
      // Incognito has no effect on blank creation.
      $('open-image-incognito-row').style.display = blank ? 'none' : '';
      const showReplace = name === 'file' && canReplace();
      replaceRow.style.display = showReplace ? '' : 'none';
      replaceBtn.style.display = showReplace ? '' : 'none';
      // A URL never auto-previews.
      urlPreviewLoaded = urlPreviewShown = false;
      refresh();
      if (activeTab === 'url') syncPreview(); else loadPreviewMedia();
    };

    const { open, close, openPopover } = wireModalShell(overlay, $('load-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        urlEl.value = '';
        if (frameEl) frameEl.value = '0';
        // Full reset, so a prior open's image never leaks in.
        urlPreviewLoaded = urlPreviewShown = false;
        revokePreviewUrl();
        cropToggle.checked = false;
        cropIw = cropIh = 0;
        cropRect = { x: 0, y: 0, width: 0, height: 0 };
        previewImg.removeAttribute('src');
        previewVideo.pause();
        previewVideo.removeAttribute('src');
        previewWrap.style.display = 'none';
        cropRow.style.display = 'none';
        incog.checked = false;
        colorEl.value = '#ffffff';
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
      },
      onClose: () => {
        revokePreviewUrl();
        previewVideo.pause();
      }
    });
    // `from` = the control that asked, so the dialog grows out of that (the idle canvas card,
    // the projects footer button) rather than the toolbar icon.
    const openBlank = (from) => { open(from); setTab('blank'); };

    cancelBtn.addEventListener('click', close);
    // The open-ANOTHER icon answers the same gestures, and the full open grows out of it too:
    // the shell's own opener (#load-image-btn) is display:none once an image exists.
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

    fileEl.addEventListener('change', () => { refresh(); loadPreviewMedia(); });
    // Editing the URL retires the preview (no crop/scrubber) without taking it off the screen.
    urlEl.addEventListener('input', () => { urlPreviewLoaded = false; refresh(); syncPreview(); });
    const doUrlPreview = () => {
      if (!isPreviewableUrl(urlVal())) { notify('Enter a valid image or video URL (http/https or data:).', 'fail'); return; }
      urlPreviewLoaded = urlPreviewShown = true;
      refresh();
      loadPreviewMedia();
    };
    urlPreviewBtn.addEventListener('click', doUrlPreview);
    urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); doUrlPreview(); } });

    cropToggle.addEventListener('change', syncPreview);
    orientBtn.addEventListener('click', () => { cropAlbum = !cropAlbum; recenterCrop(); });

    // When cropping, a settled seek re-captures the still so the crop tracks the frame.
    frameEl.addEventListener('input', () => {
      if (isVideoSource() && previewVideo.readyState) {
        const t = Number(frameEl.value) || 0;
        try { previewVideo.currentTime = t; } catch { /* ignore out-of-range seeks */ }
      }
    });
    previewVideo.addEventListener('seeked', () => {
      frameEl.value = String(Math.round(previewVideo.currentTime * 10) / 10);
      if (cropEnabled()) captureVideoFrameForCrop();
    });

    // Move / corner-resize over the preview (mirrors cropModal).
    const toImage = (clientX, clientY) => {
      const r = previewImg.getBoundingClientRect();
      return { x: (clientX - r.left) / cropScale, y: (clientY - r.top) / cropScale };
    };
    let drag = null; // { kind: 'move'|'resize', corner, startImg, startRect }
    const onDown = (e, kind, corner) => {
      e.preventDefault();
      e.stopPropagation();
      drag = { kind, corner, startImg: toImage(e.clientX, e.clientY), startRect: { ...cropRect } };
      document.addEventListener('mousemove', onMove);
      document.addEventListener('mouseup', onUp);
    };
    const onMove = e => {
      if (!drag) return;
      const cur = toImage(e.clientX, e.clientY);
      if (drag.kind === 'move') {
        cropRect = moveCropClamped(drag.startRect, cur.x - drag.startImg.x, cur.y - drag.startImg.y, cropIw, cropIh);
      } else {
        cropRect = resizeCropFromCorner(drag.startRect, drag.corner, cur.x, cur.y, cropAspectV, cropIw, cropIh);
      }
      renderCropBox();
    };
    const onUp = () => {
      drag = null;
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
    };
    cropBox.addEventListener('mousedown', e => onDown(e, 'move'));
    cropBox.querySelectorAll('.crop-handle').forEach(h =>
      h.addEventListener('mousedown', e => onDown(e, 'resize', parseInt(h.dataset.corner, 10))));

    $('blank-image-white').addEventListener('click', () => { colorEl.value = '#ffffff'; });
    $('blank-image-black').addEventListener('click', () => { colorEl.value = '#000000'; });

    const toFrameIfVideo = (file) => frameIfVideo(file, Number(frameEl && frameEl.value) || 0);

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
      const w = parseInt(widthEl.value), h = parseInt(heightEl.value);
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
