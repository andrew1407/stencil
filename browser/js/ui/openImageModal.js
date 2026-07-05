import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { defaultBlankSizePx } from '../core/layout.js';
import { icon } from './icons.js';
import { isVideoFile, videoFileToImageFile } from '../core/videoFrame.js';
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

                <!-- Tab: URL link -->
                <div class="oi-panel" id="oi-panel-url" style="display:none">
                    <div class="vs-row vs-field"><label title="Load an image or video straight from the web">URL</label><input type="url" id="open-image-url" placeholder="https://… (image or video)"></div>
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

                <!-- Frame time: shown when the file/URL source is a video (a still frame is captured). -->
                <div class="vs-row" id="open-image-frame-row" style="display:none">
                    <label title="Capture the frame at this time (seconds)">Frame (s)</label>
                    <input type="number" id="open-image-frame" min="0" step="0.1" value="0" style="width:6rem">
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
    const VIDEO_URL = /\.(mp4|mov|webm|mkv|avi|m4v|mpe?g)(\?|#|$)/i;
    const urlVal = () => urlEl.value.trim();
    const chosenFile = () => fileEl.files && fileEl.files[0];
    const hasSource = () => (activeTab === 'file' ? !!chosenFile() : urlVal() !== '');
    const isVideoSource = () => (activeTab === 'url'
      ? VIDEO_URL.test(urlVal())
      : !!chosenFile() && isVideoFile(chosenFile()));

    // Enable the file/URL action buttons once a source is chosen; reveal the frame row
    // for a video source. (Blank tab has its own Create button and no source concept.)
    const refresh = () => {
      if (activeTab === 'blank') { frameRow.style.display = 'none'; return; }
      const has = hasSource();
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
      replaceBtn.disabled = !has || activeTab === 'url' || isVideoSource() || !canReplace();
      frameRow.style.display = isVideoSource() ? '' : 'none';
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
      refresh();
    };

    // The toolbar's Open button (empty state) is this modal's primary trigger.
    const { open, close } = wireModalShell(overlay, $('load-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        urlEl.value = '';
        if (frameEl) frameEl.value = '0';   // reset the video frame-time (full-reset contract)
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

    fileEl.addEventListener('change', refresh);
    urlEl.addEventListener('input', refresh);

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
      const resolved = await resolveSource();
      if (!resolved) return;
      app.openImageHere(resolved, incog.checked, address);
      close();
    });
    newTabBtn.addEventListener('click', async () => {
      if (!hasSource()) return;
      const resolved = await resolveSource();
      if (!resolved) return;
      app.openImageNewTab(resolved, incog.checked);
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
