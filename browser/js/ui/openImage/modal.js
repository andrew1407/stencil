import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from '../base.js';
import { wireModalOpenGestures } from '../popover.js';
import { spinIconOnce } from '../icons.js';
import { isVideoFile, isVideoUrl } from '../../core/videoFrame.js';
import { createCropOverlay, freshCropState, hasCropRect } from './crop.js';
import { modalBoxEase } from '../motion/easeBoxHeight.js';
import { openImageModalInner } from './markup.js';
import { createPreviewDust } from './previewDust.js';
import { createFrameScrub } from './frameScrub.js';
import { createCropRows } from './cropRows.js';
import { createOpenImageTabs } from './tabs.js';
import { wireOpenActions } from './actions.js';
import { notify } from '../../utils.js';

// The single way to get an image into the editor. The three tabs are their own elements under
// sources/ and speak up on the bubbling `source-change` / `preview-request` / `create-blank`;
// this window owns the preview, the crop and the footer. The DOM is built once and reused, so
// onOpen MUST reset every field.
export class StencilOpenImageModal extends StencilElement {
  static inner() { return openImageModalInner(); }
  static template() { return hostTag('stencil-open-image-modal', 'id="open-image-modal-overlay" class="app-modal-overlay"', StencilOpenImageModal.inner()); }

  wire(app) {
    const overlay = this;
    const fileSrc = this.querySelector('stencil-oi-file-source');
    const urlSrc = this.querySelector('stencil-oi-url-source');
    const blank = this.querySelector('stencil-oi-blank-tab');
    const tabs = this.querySelector('stencil-tabs');
    const incog = this.$('open-image-incognito');
    const hereBtn = this.$('open-image-here'), newTabBtn = this.$('open-image-newtab');
    const replaceBtn = this.$('open-image-replace'), replaceRow = this.$('open-image-replace-row');
    const renameEl = this.$('open-image-rename'), keepEl = this.$('open-image-keep');
    const targetEl = this.$('open-image-target'), targetRow = this.$('open-image-target-row');
    const frameEl = this.$('open-image-frame'), frameRow = this.$('open-image-frame-row');
    const scrubEl = this.$('open-image-frame-scrub');
    const cropToggle = this.$('open-image-crop-toggle');
    const cropDims = this.$('open-image-crop-dims'), orientBtn = this.$('open-image-crop-orientation');
    const target = () => (targetEl && targetEl.value) || null;
    const boxEase = modalBoxEase(overlay);   // the box eases between its content heights

    // Only a saved local or server-linked project — not a blank / incognito session.
    const canReplace = () => !!(app.image && !app.storage.incognito
      && (app.activeProjectId != null || app.remoteLink));

    const urlVal = () => urlSrc.url;
    const chosenFile = () => fileSrc.file;
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

    // The crop rect and everything that draws it (crop.js); the tabs and the media the rect is
    // drawn ON are tabs.js.
    const cropState = freshCropState();
    const crop = createCropOverlay({
      state: cropState,
      els: { box: this.$('open-image-crop-box'), shade: this.$('open-image-crop-shade'), dims: cropDims, orient: orientBtn },
      pageDims: () => cropRows.pageDims(), media: () => preview.cropMedia(),
      onDrag: () => preview.persistCropRect(),
    });
    const cropRows = createCropRows({
      app,
      els: { cropDims, orientBtn, cropSizeRow: this.$('open-image-crop-size-row'),
        cropSizeCustom: this.$('open-image-crop-size-custom'), cropSizeSel: this.$('open-image-crop-size'),
        cropSizeW: this.$('open-image-crop-size-w'), cropSizeH: this.$('open-image-crop-size-h') },
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
        previewImg: this.$('open-image-preview-img'), previewVideo: this.$('open-image-preview-video'),
        previewWrap: this.$('open-image-preview'), statusEl: this.$('open-image-preview-status'),
        cropStage: this.$('open-image-crop-stage'), cropRow: this.$('open-image-crop-row'),
        cropToggle, scrubEl, chooseBtn: fileSrc.field, urlEl: urlSrc.field,
        hereBtn, newTabBtn, replaceRow, replaceBtn,
        incogRow: this.$('open-image-incognito-row'), createBtn: this.$('blank-image-create'),
      },
      src, cropState, crop, cropRows, dust, frame, canReplace, refresh: () => refresh(),
    });

    // Blank tab has its own Create button and no source concept.
    const refresh = () => {
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

    const { open, close, openPopover } = wireModalShell(overlay, document.getElementById('load-image-btn'), this.$('open-image-close'), {
      onOpen: () => {
        fileSrc.reset();
        urlSrc.reset();
        if (frameEl) frameEl.value = '0';
        preview.reset();
        Object.assign(cropState, freshCropState());
        cropRows.reset();
        cropRows.hideAll();
        incog.checked = false;
        blank.reset(cropRows.pageDims());
        renameEl.checked = false;
        keepEl.checked = true;
        hereBtn.disabled = true;
        newTabBtn.disabled = true;
        replaceBtn.disabled = true;
        // Incognito content isn't created on a server.
        fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
        tabs.select('file');
        boxEase.start();
      },
      onClose: () => { preview.closeUp(); boxEase.stop(); },
    });
    wireOpenActions({
      app, preview, src, canReplace, openOpts, target, close: () => close(),
      els: { hereBtn, newTabBtn, replaceBtn, incog, renameEl, keepEl },
      frameSeconds: () => frame.frameSeconds(),
    });

    // `from` = the control that asked, so the dialog grows out of that, not the toolbar icon.
    const openBlank = (from) => { open(from); tabs.select('blank'); };

    this.$('open-image-cancel').addEventListener('click', close);
    // The open-ANOTHER icon answers the same gestures too (#load-image-btn hides once an image exists).
    const anotherBtn = document.getElementById('open-image-btn');
    if (anotherBtn) wireModalOpenGestures(anotherBtn, { openFull: () => open(anotherBtn), openPopover: () => openPopover(anotherBtn) });
    const blankBtn = document.getElementById('create-blank-btn');
    blankBtn?.addEventListener('click', () => openBlank(blankBtn));
    const projectsBlank = document.getElementById('projects-blank-image');
    projectsBlank?.addEventListener('click', () => {
      // Close the projects modal via its own close, so its handlers run.
      document.getElementById('projects-close')?.click();
      openBlank(projectsBlank);
    });

    // ── What the tabs say ──────────────────────────────────────────────
    this.addEventListener('tab-change', (e) => preview.setTab(e.detail.tab));
    this.addEventListener('source-change', (e) => {
      if (e.detail.kind === 'file') {
        refresh();
        preview.loadPreviewMedia(/*replacing=*/true);
        return;
      }
      // Editing the URL retires the preview (no crop/scrubber) without taking it off the screen.
      preview.retireUrlPreview();
      refresh();
      preview.syncPreview();
    });
    this.addEventListener('preview-request', () => {
      preview.markUrlPreviewed();
      refresh();
      preview.loadPreviewMedia(/*replacing=*/true);
    });
    this.addEventListener('create-blank', async (e) => {
      const { color, width, height } = e.detail;
      if (app.image && !(await app.confirm('Replace the current image with a new blank image?', { title: 'Replace image', confirmIcon: 'swap' }))) return;
      app.createBlankImage({ color, width, height, address: target() || undefined })
        .then(() => { close(); notify(`Blank ${width}×${height} image created`, 'ok'); })
        .catch((err) => notify(err && err.message ? err.message : 'Could not create the image', 'fail'));
    });
    this.$('blank-image-create').addEventListener('click', () => blank.requestCreate());

    // Incognito and a server target are mutually exclusive.
    incog.addEventListener('change', () => {
      fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
    });

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
