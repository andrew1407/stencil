import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from '../base.js';
import { wireModalOpenGestures } from '../popover.js';
import { notify } from '../../utils.js';
import { spinIconOnce } from '../icons.js';
import { isVideoFile, isVideoUrl } from '../../core/videoFrame.js';
import { createCropOverlay, freshCropState, hasCropRect } from './crop.js';
import { modalBoxEase } from '../motion/easeBoxHeight.js';
import { openImageModalInner } from './markup.js';
import { createPreviewDust } from './previewDust.js';
import { createFrameScrub } from './frameScrub.js';
import { createCropRows } from './cropRows.js';
import { createOpenImageTabs } from './tabs.js';
import { createBlankTab } from './blank.js';
import { wireOpenActions } from './actions.js';

// The single way to get an image into the editor (Local file / URL link / Blank tabs); the DOM is built once and reused, so onOpen MUST reset every field.
export class StencilOpenImageModal extends StencilElement {
  static inner() { return openImageModalInner(); }
  static template() { return hostTag('stencil-open-image-modal', 'id="open-image-modal-overlay" class="app-modal-overlay"', StencilOpenImageModal.inner()); }

  wire(app) {
    const $ = id => document.getElementById(id);
    const overlay = $('open-image-modal-overlay');
    const closeBtn = $('open-image-close');
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
    const hereBtn = $('open-image-here'), newTabBtn = $('open-image-newtab');
    const replaceBtn = $('open-image-replace'), replaceRow = $('open-image-replace-row');
    const renameEl = $('open-image-rename'), keepEl = $('open-image-keep');
    const targetEl = $('open-image-target'), targetRow = $('open-image-target-row');
    const frameEl = $('open-image-frame'), frameRow = $('open-image-frame-row');
    const scrubEl = $('open-image-frame-scrub');
    const cropToggle = $('open-image-crop-toggle');
    const cropDims = $('open-image-crop-dims'), orientBtn = $('open-image-crop-orientation');
    const target = () => (targetEl && targetEl.value) || null;
    const boxEase = modalBoxEase(overlay);   // the box eases between its content heights

    // Only a saved local or server-linked project — not a blank / incognito session.
    const canReplace = () => !!(app.image && !app.storage.incognito
      && (app.activeProjectId != null || app.remoteLink));

    const urlVal = () => urlEl.value.trim();
    const chosenFile = () => fileEl.files && fileEl.files[0];
    // What the chosen source IS, asked per tab — the tab controller carries the active one.
    const src = {
      urlVal, chosenFile,
      hasSource: () => (preview.tab() === 'file' ? !!chosenFile() : urlVal() !== ''),
      isVideoOn: (tab) => (tab === 'url'
        ? isVideoUrl(urlVal())
        : !!chosenFile() && isVideoFile(chosenFile())),
      keyOn: (tab) => (tab === 'file'
        ? (chosenFile() ? `${chosenFile().name}:${chosenFile().size}:${chosenFile().lastModified}` : '')
        : urlVal()),
    };
    // A half-typed URL never triggers a fetch: the explicit Preview button is gated on this.
    const isPreviewableUrl = (v) => {
      if (!v) return false;
      try { return /^(https?:|data:|blob:)$/i.test(new URL(v).protocol); } catch { return false; }
    };

    // The crop rect and everything that draws it (openImageCrop.js); the tabs and the media
    // the rect is drawn ON are openImageTabs.js.
    const cropState = freshCropState();
    const crop = createCropOverlay({
      state: cropState,
      els: { box: $('open-image-crop-box'), shade: $('open-image-crop-shade'), dims: cropDims, orient: orientBtn },
      pageDims: () => cropRows.pageDims(), media: () => preview.cropMedia(),
      onDrag: () => preview.persistCropRect(),
    });
    const cropRows = createCropRows({
      app,
      els: { cropDims, orientBtn, cropSizeRow: $('open-image-crop-size-row'),
        cropSizeCustom: $('open-image-crop-size-custom'), cropSizeSel: $('open-image-crop-size'),
        cropSizeW: $('open-image-crop-size-w'), cropSizeH: $('open-image-crop-size-h') },
      fitToPage: () => crop.fitToPage(), persist: () => preview.persistCropRect(),
    });
    const dust = createPreviewDust({
      img: () => preview.img(), video: () => preview.video(),
      isVideo: () => preview.isVideoTab(), sourceKey: () => preview.sourceKey(),
    });
    const frame = createFrameScrub({
      frameEl, scrubEl, video: () => preview.video(),
      cropMedia: () => preview.cropMedia(), isVideoSource: () => preview.isVideoSource(),
    });
    const preview = createOpenImageTabs({
      els: {
        previewImg: $('open-image-preview-img'), previewVideo: $('open-image-preview-video'),
        previewWrap: $('open-image-preview'), statusEl: $('open-image-preview-status'),
        cropStage: $('open-image-crop-stage'), cropRow: $('open-image-crop-row'),
        cropToggle, scrubEl, chooseBtn, urlEl, hereBtn, newTabBtn, replaceRow, replaceBtn,
        tabBtns: [$('oi-tab-file'), $('oi-tab-url'), $('oi-tab-blank')],
        panels: { file: $('oi-panel-file'), url: $('oi-panel-url'), blank: $('oi-panel-blank') },
        incogRow: $('open-image-incognito-row'), createBtn: $('blank-image-create'),
      },
      src, cropState, crop, cropRows, dust, frame, canReplace, refresh: () => refresh(),
    });

    // Blank tab has its own Create button and no source concept.
    const refresh = () => {
      urlPreviewBtn.disabled = !isPreviewableUrl(urlVal());
      if (preview.tab() === 'blank') { frameRow.style.display = 'none'; return; }
      const has = src.hasSource();
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
      replaceBtn.disabled = !has || preview.tab() === 'url' || preview.isVideoSource() || !canReplace();
      // A URL video has no scrubber until Preview.
      frameRow.style.display = (preview.isVideoSource() && preview.previewReady() && preview.ready()) ? '' : 'none';
    };

    const cropEnabled = () => cropToggle.checked && preview.previewReady();
    // Crop on + measured → the chosen rect, else `noCrop`; a URL carries its own URL too.
    const openOpts = () => {
      const o = (cropEnabled() && hasCropRect(cropState))
        ? { crop: { ...cropState.rect } } : { noCrop: true };
      if (preview.tab() === 'url') o.source = urlVal();
      return o;
    };

    const { open, close, openPopover } = wireModalShell(overlay, $('load-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        showFileName();
        urlEl.value = '';
        if (frameEl) frameEl.value = '0';
        preview.reset();
        Object.assign(cropState, freshCropState());
        cropRows.reset();
        cropRows.hideAll();
        incog.checked = false;
        blank.reset();
        renameEl.checked = false;
        keepEl.checked = true;
        hereBtn.disabled = true;
        newTabBtn.disabled = true;
        replaceBtn.disabled = true;
        // Incognito content isn't created on a server.
        fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
        preview.setTab('file');
        boxEase.start();
      },
      onClose: () => { preview.closeUp(); boxEase.stop(); },
    });
    const blank = createBlankTab({
      app, close: () => close(), target,
      els: { colorEl: $('blank-image-color'), colorHexEl: $('blank-image-color-hex'),
        widthEl: $('blank-image-width'), heightEl: $('blank-image-height'),
        createBtn: $('blank-image-create'), whiteBtn: $('blank-image-white'), blackBtn: $('blank-image-black') },
      pageDims: () => cropRows.pageDims(),
    });
    wireOpenActions({
      app, preview, src, canReplace, openOpts, target, close: () => close(),
      els: { hereBtn, newTabBtn, replaceBtn, incog, renameEl, keepEl },
      frameSeconds: () => frame.frameSeconds(),
    });

    // `from` = the control that asked, so the dialog grows out of that, not the toolbar icon.
    const openBlank = (from) => { open(from); preview.setTab('blank'); };

    $('open-image-cancel').addEventListener('click', close);
    // The open-ANOTHER icon answers the same gestures too (#load-image-btn hides once an image exists).
    const anotherBtn = $('open-image-btn');
    if (anotherBtn) wireModalOpenGestures(anotherBtn, { openFull: () => open(anotherBtn), openPopover: () => openPopover(anotherBtn) });
    $('create-blank-btn')?.addEventListener('click', () => openBlank($('create-blank-btn')));
    // Close the projects modal via its own close, so its handlers run.
    $('projects-blank-image')?.addEventListener('click', () => {
      $('projects-close')?.click();
      openBlank($('projects-blank-image'));
    });

    for (const t of [$('oi-tab-file'), $('oi-tab-url'), $('oi-tab-blank')])
      t.addEventListener('click', () => preview.setTab(t.dataset.tab));

    // Incognito and a server target are mutually exclusive.
    incog.addEventListener('change', () => {
      fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
    });

    // Bound to the NAME, not the box, or a click on the button would open the picker twice
    // (desktop twin: clickActivates on the dialog's path field).
    for (const el of [chooseBtn, fileNameEl]) el.addEventListener('click', () => fileEl.click());
    fileEl.addEventListener('change', () => {
      showFileName();
      refresh();
      preview.loadPreviewMedia(/*replacing=*/true);
    });
    // Editing the URL retires the preview (no crop/scrubber) without taking it off the screen.
    urlEl.addEventListener('input', () => { preview.retireUrlPreview(); refresh(); preview.syncPreview(); });
    const doUrlPreview = () => {
      if (!isPreviewableUrl(urlVal())) { notify('Enter a valid image or video URL (http/https or data:).', 'fail'); return; }
      preview.markUrlPreviewed();
      refresh();
      preview.loadPreviewMedia(/*replacing=*/true);
    };
    urlPreviewBtn.addEventListener('click', doUrlPreview);
    urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); doUrlPreview(); } });

    cropToggle.addEventListener('change', () => {
      preview.rememberCropChoice();
      cropRows.markUserChange();   // THIS is the change the read-out's play belongs to
      preview.syncPreview();
    });
    // spun AFTER the repaint: crop.recenter() rewrites the glyph, dropping a running turn.
    orientBtn.addEventListener('click', () => {
      cropState.album = !cropState.album;
      crop.recenter(true); preview.persistCropRect(); spinIconOnce(orientBtn);
    });
  }
}
define('stencil-open-image-modal', StencilOpenImageModal);
