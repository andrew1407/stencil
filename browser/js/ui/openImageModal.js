import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { defaultBlankSizePx } from '../core/layout.js';
import { icon } from './icons.js';
import { isVideoFile, isVideoUrl, videoFileToImageFile, videoFrameDataUrl } from '../core/videoFrame.js';
import { cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped, isAlbumOrientation } from '../core/cropGeometry.js';
const { PAGE_SIZES } = constants;

// ── Component: unified "Open Image" dialog ──────────────────────
// The SINGLE way to get an image into the editor, presented as three tabs:
//   • Local file — pick an image/video file
//   • URL link   — load an image/video straight from the web
//   • Blank      — create a solid-color canvas
// It replaces the old split of {toolbar file picker, "open another image", the Links
// modal's add-by-URL section, and the separate Blank Image modal}. Opened from the
// toolbar's top-left Open button; the blank shortcuts (idle canvas + projects footer)
// open it on the Blank tab.
//
// The modal DOM is built once and reused for the app's lifetime, so onOpen MUST reset
// every field (active tab, file, url, blank color/size) — otherwise the previous
// open's input leaks into the next.
export class StencilOpenImageModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('image', { size: 18 })} Open Image</h2>
                <button class="app-modal-close btn-icon-text" id="open-image-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <!-- Source tabs: pick how to add an image. -->
                <div class="oi-tabs" role="tablist">
                    <button class="oi-tab is-active" id="oi-tab-file" data-tab="file" role="tab" type="button">${icon('file-text', { size: 14 })}<span>Local file</span></button>
                    <button class="oi-tab" id="oi-tab-url" data-tab="url" role="tab" type="button">${icon('link', { size: 14 })}<span>URL link</span></button>
                    <button class="oi-tab" id="oi-tab-blank" data-tab="blank" role="tab" type="button">${icon('plus-circle', { size: 14 })}<span>Blank</span></button>
                </div>

                <!-- Tab: Local file -->
                <div class="oi-panel" id="oi-panel-file">
                    <div class="vs-row"><label>Choose</label><input type="file" id="open-image-file" accept="image/*,video/*"></div>
                </div>

                <!-- Tab: URL link. Preview is explicit (button / Enter) after validation — not
                     on every keystroke — so a half-typed URL never spins up a fetch. -->
                <div class="oi-panel" id="oi-panel-url" style="display:none">
                    <div class="vs-row vs-field"><label title="Load an image or video straight from the web">URL</label><input type="url" id="open-image-url" placeholder="https://… (image or video)"><button id="open-image-url-preview" class="btn-icon-text" type="button" title="Load a preview of this URL" disabled>${icon('image', { size: 14 })}<span>Preview</span></button></div>
                </div>

                <!-- Tab: Blank -->
                <div class="oi-panel" id="oi-panel-blank" style="display:none">
                    <div class="vs-section">Fill color</div>
                    <div class="vs-row"><label>Presets</label>
                        <span class="bi-presets">
                            <button id="blank-image-white" class="bi-preset bi-preset-white" type="button" title="Fill with white">White</button>
                            <button id="blank-image-black" class="bi-preset bi-preset-black" type="button" title="Fill with black">Black</button>
                        </span>
                    </div>
                    <div class="vs-row"><label>Custom color</label><input type="color" id="blank-image-color" value="#ffffff"></div>
                    <div class="vs-section">Size (px)</div>
                    <div class="vs-row"><label>Width</label><input type="number" id="blank-image-width" min="1" max="8192"></div>
                    <div class="vs-row"><label>Height</label><input type="number" id="blank-image-height" min="1" max="8192"></div>
                </div>

                <!-- Live preview of the chosen file/URL source (blank tab has none). A video
                     shows a scrubber to pick the frame; the still <img> below is also the
                     inline crop stage (the crop box/handles overlay it when Crop is on). -->
                <div class="oi-preview" id="open-image-preview" style="display:none">
                    <video id="open-image-preview-video" controls muted playsinline preload="auto" style="display:none;max-width:100%;max-height:38vh;background:#222;"></video>
                    <div id="open-image-crop-stage" style="position:relative;display:none;line-height:0;max-width:100%;background:#222;">
                        <img id="open-image-preview-img" alt="Preview" style="display:block;width:auto;height:auto;max-width:100%;max-height:38vh;user-select:none;-webkit-user-drag:none;">
                        <div id="open-image-crop-shade-clip" style="position:absolute;inset:0;overflow:hidden;pointer-events:none;">
                            <div id="open-image-crop-shade" style="position:absolute;box-shadow:0 0 0 9999px rgba(0,0,0,0.45);display:none;"></div>
                        </div>
                        <div id="open-image-crop-box" style="position:absolute;box-sizing:border-box;border:2px solid #4da3ff;cursor:move;display:none;">
                            <span class="crop-handle" data-corner="0" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;left:-8px;top:-8px;cursor:nwse-resize;"></span>
                            <span class="crop-handle" data-corner="1" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;right:-8px;top:-8px;cursor:nesw-resize;"></span>
                            <span class="crop-handle" data-corner="2" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;right:-8px;bottom:-8px;cursor:nwse-resize;"></span>
                            <span class="crop-handle" data-corner="3" style="position:absolute;width:14px;height:14px;background:#4da3ff;border:2px solid #fff;border-radius:50%;left:-8px;bottom:-8px;cursor:nesw-resize;"></span>
                        </div>
                    </div>
                    <div id="open-image-crop-dims" style="font-size:13px;color:var(--text-muted);display:none;"></div>
                </div>

                <!-- Frame time: shown when the file/URL source is a video (a still frame is captured). -->
                <div class="vs-row" id="open-image-frame-row" style="display:none">
                    <label title="Capture the frame at this time (seconds)">Frame (s)</label>
                    <input type="number" id="open-image-frame" min="0" step="0.1" value="0" style="width:6rem">
                </div>

                <!-- Crop before opening. Unchecked by default; checked reveals the inline crop
                     editor over the preview (aspect locked to the page, Album/Portrait toggle),
                     matching the standalone Crop modal's model. -->
                <div class="vs-row" id="open-image-crop-row" style="display:none">
                    <label title="Crop the image to the page aspect before opening">Crop</label>
                    <span class="oi-crop-opt">
                        <input type="checkbox" id="open-image-crop-toggle">
                        <span class="footer-hint">Trim to the page aspect before opening.</span>
                        <button id="open-image-crop-orientation" class="btn-icon-text" type="button" title="Swap album / portrait — flips the crop orientation" style="display:none">${icon('swap', { size: 14 })}<span>Album</span></button>
                    </span>
                </div>

                <!-- ── Common options ── -->
                <!-- Incognito applies to a file/URL open; a new blank never supported it
                     (create the blank, then toggle incognito) so the row hides on that tab. -->
                <div class="vs-row" id="open-image-incognito-row">
                    <label>Incognito</label>
                    <span class="oi-incognito">
                        <input type="checkbox" id="open-image-incognito">
                        <span class="footer-hint">Edit without saving — the image is never written to storage.</span>
                    </span>
                </div>
                <!-- Save target: only shown when at least one server is connected. -->
                <div class="vs-row" id="open-image-target-row" style="display:none">
                    <label title="Open here locally or create on a connected server">Save to</label>
                    <select id="open-image-target"></select>
                </div>
                <!-- Replace options: only shown on the Local file tab over a replaceable project. -->
                <div class="vs-row" id="open-image-replace-row" style="display:none">
                    <label title="Swap this project's image, keeping the same project">Replace</label>
                    <span class="oi-replace">
                        <label class="vs-inline-check"><input type="checkbox" id="open-image-rename"> Rename project to the new image</label>
                        <label class="vs-inline-check"><input type="checkbox" id="open-image-keep" checked> Keep existing annotations</label>
                    </span>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint"></span>
                <button id="open-image-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                <button id="open-image-replace" class="btn-icon-text" disabled style="display:none">${icon('refresh', { size: 14 })}<span>Replace image</span></button>
                <button id="open-image-here" class="btn-icon-text" disabled>${icon('image', { size: 14 })}<span>Open here</span></button>
                <button id="open-image-newtab" class="btn-icon-text" disabled>${icon('external', { size: 14 })}<span>Open in new tab</span></button>
                <button id="blank-image-create" class="btn-icon-text" style="display:none">${icon('image', { size: 14 })}<span>Create blank</span></button>
            </div>
        </div>
    `;
  }
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
    // Preview + inline crop editor.
    const previewWrap = $('open-image-preview');
    const previewImg = $('open-image-preview-img');
    const previewVideo = $('open-image-preview-video');
    const cropRow = $('open-image-crop-row');
    const cropToggle = $('open-image-crop-toggle');
    const cropStage = $('open-image-crop-stage');
    const cropBox = $('open-image-crop-box');
    const cropShade = $('open-image-crop-shade');   // clipped dimming backdrop (mirrors box)
    const cropDims = $('open-image-crop-dims');
    const orientBtn = $('open-image-crop-orientation');
    // Tabs + panels.
    const tabs = [$('oi-tab-file'), $('oi-tab-url'), $('oi-tab-blank')];
    const panels = { file: $('oi-panel-file'), url: $('oi-panel-url'), blank: $('oi-panel-blank') };
    // Blank controls (folded in from the retired blank-image modal).
    const colorEl = $('blank-image-color');
    const widthEl = $('blank-image-width');
    const heightEl = $('blank-image-height');
    const createBtn = $('blank-image-create');

    let activeTab = 'file';

    // Replace-in-place only applies to a saved local or server-linked project (not a blank /
    // incognito session — there's nothing to keep the same).
    const canReplace = () => !!(app.image && !app.storage.incognito
      && (app.activeProjectId != null || app.remoteLink));

    // Page dimensions for blank-size defaults. NOT getPageDimensions(): that swaps to
    // landscape from the CURRENT canvas aspect — use the page as selected.
    const pageDims = () => (app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);

    // Source helpers scoped to the active tab (file vs url).
    const urlVal = () => urlEl.value.trim();
    const chosenFile = () => fileEl.files && fileEl.files[0];
    const hasSource = () => (activeTab === 'file' ? !!chosenFile() : urlVal() !== '');
    const isVideoSource = () => (activeTab === 'url'
      ? isVideoUrl(urlVal())
      : !!chosenFile() && isVideoFile(chosenFile()));
    // A URL is previewable only once it's a well-formed http(s)/data:/blob: URL — the guard
    // behind the explicit Preview button (a half-typed URL never triggers a fetch).
    const isPreviewableUrl = (v) => {
      if (!v) return false;
      try { return /^(https?:|data:|blob:)$/i.test(new URL(v).protocol); } catch { return false; }
    };
    // Is the preview area live? A local file previews as soon as it's chosen; a URL only after
    // Preview is pressed. Gates the preview/crop UI (open still works straight from the URL).
    const previewReady = () => (activeTab === 'file' ? !!chosenFile() : activeTab === 'url' && urlPreviewLoaded);

    // ── Inline preview + crop editor state (all rect math in ORIGINAL-image pixels,
    //    i.e. the natural pixels of the still that will be imported). ──
    let previewObjectUrl = null;   // object URL backing the <img>/<video>, revoked on swap
    let cropRect = { x: 0, y: 0, width: 0, height: 0 };
    let cropAlbum = false;
    let cropAspectV = 1;
    let cropScale = 1;         // display px per image px
    let cropIw = 0, cropIh = 0; // still-image natural dimensions
    // A URL source previews only after the user presses Preview (validated) — never on
    // keystroke. This tracks whether that preview is currently live for the typed URL.
    let urlPreviewLoaded = false;

    const revokePreviewUrl = () => { if (previewObjectUrl) { URL.revokeObjectURL(previewObjectUrl); previewObjectUrl = null; } };

    // Position the crop box + its dimming backdrop over the preview image (mirrors cropModal).
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

    // Re-fit a centered crop for the current orientation (used on init + on flip).
    const recenterCrop = () => {
      cropAspectV = cropAspect(pageDims().width, pageDims().height, cropAlbum);
      cropRect = centeredCrop(cropIw, cropIh, cropAspectV);
      renderCropBox();
    };

    // The still preview <img> finished (re)loading: (re)fit the crop to the page aspect when
    // the geometry is new (a fresh image / first video frame), else keep the user's rect —
    // every frame of one video shares the same captured dimensions.
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

    // Whether the crop editor should be visible: crop enabled on a live preview.
    const cropEnabled = () => cropToggle.checked && previewReady();

    // Capture the current video frame to the preview <img> (the crop stage), reusing the
    // shared frame extractor so the cropped pixels match exactly what import will capture.
    const captureVideoFrameForCrop = () => {
      const src = activeTab === 'file' ? URL.createObjectURL(chosenFile()) : urlVal();
      videoFrameDataUrl(src, Number(frameEl && frameEl.value) || 0)
        .then(dataUrl => { previewImg.src = dataUrl; })  // load handler fits the crop
        .catch(e => {
          notify(`Could not read that video frame for cropping — ${e.message}`, 'fail');
          cropToggle.checked = false;
          syncPreview();
        });
    };

    // Hide the whole crop overlay (box + dimming backdrop + dims readout) in one call.
    const hideCropOverlay = () => {
      cropBox.style.display = 'none';
      cropShade.style.display = 'none';
      cropDims.style.display = 'none';
    };

    // Reflect the current source + crop state into the preview area (visibility + sources).
    const syncPreview = () => {
      const show = previewReady();
      previewWrap.style.display = show ? '' : 'none';
      cropRow.style.display = show ? '' : 'none';
      orientBtn.style.display = show && cropToggle.checked ? '' : 'none';
      if (!show) { hideCropOverlay(); return; }
      const video = isVideoSource();
      const cropping = cropToggle.checked;
      // A video shows its scrubber; the crop stage (still <img>) shows for images always,
      // and for a video only while cropping (it then holds the captured frame).
      previewVideo.style.display = video ? '' : 'none';
      cropStage.style.display = (!video || cropping) ? '' : 'none';
      if (!cropping) {
        hideCropOverlay();
      } else if (cropIw && cropIh && !video && previewImg.complete && previewImg.naturalWidth) {
        // Image already loaded before Crop was ticked (its load handler ran while crop was
        // off, so no rect was fitted): fit one now, else just re-render the existing rect.
        computeCropScale();
        if (cropRect.width < 1) { cropAlbum = isAlbumOrientation(cropIw, cropIh); recenterCrop(); }
        else renderCropBox();
      }
      if (video && cropping) captureVideoFrameForCrop();
    };

    // (Re)build the preview media for the current source. Local files ride an object URL;
    // a URL loads straight into the element (display never taints, unlike a canvas readback).
    const loadPreviewMedia = () => {
      revokePreviewUrl();
      cropIw = cropIh = 0;   // force a re-fit against the new source
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

    // Enable the file/URL action buttons once a source is chosen; reveal the frame row
    // for a video source. (Blank tab has its own Create button and no source concept.)
    const refresh = () => {
      urlPreviewBtn.disabled = !isPreviewableUrl(urlVal());   // gate the explicit Preview button
      if (activeTab === 'blank') { frameRow.style.display = 'none'; return; }
      const has = hasSource();
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
      replaceBtn.disabled = !has || activeTab === 'url' || isVideoSource() || !canReplace();
      // The frame scrubber belongs to a live preview (a URL video has no scrubber until Preview).
      frameRow.style.display = (isVideoSource() && previewReady()) ? '' : 'none';
    };

    // The options passed to the open action. Crop on + measured → the chosen rect; otherwise
    // `noCrop` so the WHOLE frame imports (unchecked ⇒ no crop at all, not the default
    // page-aspect auto-crop). A URL source also carries its own URL as provenance, so a
    // dialog-opened URL image matches later (e.g. the extension's resume-by-source).
    const openOpts = () => {
      const o = (cropEnabled() && cropRect.width >= 1 && cropRect.height >= 1)
        ? { crop: { ...cropRect } } : { noCrop: true };
      if (activeTab === 'url') o.source = urlVal();
      return o;
    };

    // Activate a tab: show its panel, and swap the footer actions + replace row to match.
    const setTab = (name) => {
      activeTab = name;
      tabs.forEach(t => t.classList.toggle('is-active', t.dataset.tab === name));
      for (const [k, el] of Object.entries(panels)) el.style.display = k === name ? '' : 'none';
      const blank = name === 'blank';
      hereBtn.style.display = blank ? 'none' : '';
      newTabBtn.style.display = blank ? 'none' : '';
      createBtn.style.display = blank ? '' : 'none';
      // Incognito has no effect on blank creation — hide it there so it isn't misleading.
      $('open-image-incognito-row').style.display = blank ? 'none' : '';
      const showReplace = name === 'file' && canReplace();
      replaceRow.style.display = showReplace ? '' : 'none';
      replaceBtn.style.display = showReplace ? '' : 'none';
      // A URL never auto-previews (Preview button only); other tabs rebuild to match the source.
      urlPreviewLoaded = false;
      refresh();
      if (activeTab === 'url') syncPreview(); else loadPreviewMedia();
    };

    // The toolbar's Open button (empty state) is this modal's primary trigger.
    const { open, close } = wireModalShell(overlay, $('load-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        urlEl.value = '';
        if (frameEl) frameEl.value = '0';   // reset the video frame-time (full-reset contract)
        // Full-reset the preview + inline crop so a prior open's image never leaks in.
        urlPreviewLoaded = false;
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
        // Incognito: never offer a server target (incognito content isn't created on a server).
        fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
        setTab('file');
      },
      // Release the preview's object URL + stop any playing video when the dialog closes.
      onClose: () => {
        revokePreviewUrl();
        previewVideo.pause();
      }
    });
    // Open straight on the Blank tab (idle-canvas + projects-footer shortcuts).
    const openBlank = () => { open(); setTab('blank'); };

    // Every trigger opens THIS one dialog. Cancel is another close path.
    cancelBtn.addEventListener('click', close);
    $('open-image-btn')?.addEventListener('click', open);   // shown once an image exists
    $('create-blank-btn')?.addEventListener('click', openBlank);
    // Projects footer: close that modal (via its own close, so its handlers run) first.
    $('projects-blank-image')?.addEventListener('click', () => {
      $('projects-close')?.click();
      openBlank();
    });

    tabs.forEach(t => t.addEventListener('click', () => setTab(t.dataset.tab)));

    // Toggling incognito hides/shows the server target — the two are mutually exclusive.
    incog.addEventListener('change', () => {
      fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
    });

    fileEl.addEventListener('change', () => { refresh(); loadPreviewMedia(); });
    // Editing the URL invalidates any showing preview (it's now for a different URL) — hide it
    // until the user presses Preview again. Never auto-fetches on keystroke.
    urlEl.addEventListener('input', () => { urlPreviewLoaded = false; refresh(); syncPreview(); });
    // Explicit URL preview: validate, then load the media. Enter in the field does the same.
    const doUrlPreview = () => {
      if (!isPreviewableUrl(urlVal())) { notify('Enter a valid image or video URL (http/https or data:).', 'fail'); return; }
      urlPreviewLoaded = true;
      refresh();
      loadPreviewMedia();
    };
    urlPreviewBtn.addEventListener('click', doUrlPreview);
    urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); doUrlPreview(); } });

    // Crop toggle: reveal/hide the inline editor (capturing a video frame if needed).
    cropToggle.addEventListener('change', syncPreview);
    // Orientation flip: swap album/portrait and re-fit a centered crop (like cropModal).
    orientBtn.addEventListener('click', () => { cropAlbum = !cropAlbum; recenterCrop(); });

    // Video frame picking: the numeric input seeks the scrubber; scrubbing writes the time
    // back. When cropping, a settled seek re-captures the still so the crop tracks the frame.
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

    // ── Interactive move / corner-resize over the preview (mirrors cropModal). ──
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

    // Blank fill presets.
    $('blank-image-white').addEventListener('click', () => { colorEl.value = '#ffffff'; });
    $('blank-image-black').addEventListener('click', () => { colorEl.value = '#000000'; });

    // Fetch a URL's bytes into a File (same-origin / data: / CORS-enabled), mirroring
    // the old Links modal's honest fetch path (a canvas readback would taint without CORS).
    const fetchUrlToFile = async (url) => {
      const resp = await fetch(url);
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const blob = await resp.blob();
      const ext = (blob.type.split('/')[1] || url.split(/[?#]/)[0].split('.').pop() || 'png').slice(0, 5);
      const name = (url.split(/[?#]/)[0].split('/').pop() || 'image').replace(/\.[a-z0-9]+$/i, '') + '.' + ext;
      return new File([blob], name, { type: blob.type || 'image/png' });
    };

    // A video source is converted to a captured still frame first; images pass through.
    const toFrameIfVideo = async (file) => {
      if (!isVideoFile(file)) return file;
      return await videoFileToImageFile(file, Number(frameEl && frameEl.value) || 0);
    };

    // Resolve the active tab's source (file or URL) to a still-image File, or null on
    // error (already notified). URLs are fetched first, then treated exactly like a file.
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
      if (activeTab !== 'file' || !chosenFile() || !canReplace()) return;  // replace is local-image only
      const resolved = await resolveSource();
      if (!resolved) return;
      app.replaceProjectImage(resolved, { rename: renameEl.checked, keepAnnotations: keepEl.checked });
      close();
    });

    // Create a solid-color blank image (folded in from the old blank-image modal).
    createBtn.addEventListener('click', async () => {
      const w = parseInt(widthEl.value), h = parseInt(heightEl.value);
      if (!(w >= 1 && w <= 8192) || !(h >= 1 && h <= 8192)) {
        notify('Width and height must be 1–8192 px', 'fail');
        return;
      }
      if (app.image && !(await app.confirm('Replace the current image with a new blank image?', { title: 'Replace image' }))) return;
      const address = (targetEl && targetEl.value) || undefined;
      app.createBlankImage({ color: colorEl.value, width: w, height: h, address })
        .then(() => { close(); notify(`Blank ${w}×${h} image created`, 'ok'); })
        .catch((err) => notify(err && err.message ? err.message : 'Could not create the image', 'fail'));
    });
  }
}
define('stencil-open-image-modal', StencilOpenImageModal);
