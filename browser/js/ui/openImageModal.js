import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { isVideoFile, videoFileToImageFile } from '../core/videoFrame.js';

// ── Component: open-another-image modal ─────────────────────────
// Opened from the toolbar's #open-image-btn (only shown once an image is loaded).
// Pick a file + optionally mark it incognito, then choose to either replace the
// current editor ("Open here") or launch it in a fresh tab ("Open in new tab").
// The new-tab path rides the existing #stencil=<JSON> hand-off (applyExternalLaunch),
// which is also the only vehicle that works for incognito (never persisted).
export class StencilOpenImageModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('image', { size: 18 })} Open Another Image</h2>
                <button class="app-modal-close btn-icon-text" id="open-image-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Image or video file</div>
                <div class="vs-row"><label>Choose</label><input type="file" id="open-image-file" accept="image/*,video/*"></div>
                <!-- …or a web URL. Typing one takes precedence over the file above; a video URL
                     reveals the frame-time row just like a chosen video file. -->
                <div class="vs-row"><label title="Load an image or video straight from the web">URL</label><input type="url" id="open-image-url" placeholder="https://… (image or video)" style="flex:1"></div>
                <!-- Frame time: only shown when a video is chosen (a still frame is captured). -->
                <div class="vs-row" id="open-image-frame-row" style="display:none">
                    <label title="Capture the frame at this time (seconds)">Frame (s)</label>
                    <input type="number" id="open-image-frame" min="0" step="0.1" value="0" style="width:6rem">
                </div>
                <div class="vs-row">
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
                <!-- Replace options: only shown when a saved/linked project can be replaced. -->
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
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-open-image-modal', 'id="open-image-modal-overlay" class="app-modal-overlay"', StencilOpenImageModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('open-image-modal-overlay');
    const closeBtn = document.getElementById('open-image-close');
    const cancelBtn = document.getElementById('open-image-cancel');
    const fileEl = document.getElementById('open-image-file');
    const urlEl = document.getElementById('open-image-url');
    const incog = document.getElementById('open-image-incognito');
    const hereBtn = document.getElementById('open-image-here');
    const newTabBtn = document.getElementById('open-image-newtab');
    const replaceBtn = document.getElementById('open-image-replace');
    const replaceRow = document.getElementById('open-image-replace-row');
    const renameEl = document.getElementById('open-image-rename');
    const keepEl = document.getElementById('open-image-keep');
    const targetEl = document.getElementById('open-image-target');
    const targetRow = document.getElementById('open-image-target-row');
    const frameEl = document.getElementById('open-image-frame');
    const frameRow = document.getElementById('open-image-frame-row');

    // Replace-in-place only applies to a saved local or server-linked project (not a blank /
    // incognito session — there's nothing to keep the same).
    const canReplace = () => !!(app.image && !app.storage.incognito
      && (app.activeProjectId != null || app.remoteLink));

    // The toolbar's "Open another image" button is this modal's open trigger.
    const { open, close } = wireModalShell(overlay, document.getElementById('open-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        urlEl.value = '';
        incog.checked = false;
        hereBtn.disabled = true;
        newTabBtn.disabled = true;
        frameRow.style.display = 'none';
        const replaceable = canReplace();
        renameEl.checked = false;
        keepEl.checked = true;
        replaceRow.style.display = replaceable ? '' : 'none';
        replaceBtn.style.display = replaceable ? '' : 'none';
        replaceBtn.disabled = true;
        // Incognito: never offer a server target (incognito content isn't created on a server).
        fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
      }
    });
    // Cancel is just another close path.
    cancelBtn.addEventListener('click', close);

    // Toggling incognito hides/shows the server target — the two are mutually exclusive.
    incog.addEventListener('change', () => {
      fillTargetSelect(targetEl, targetRow, app.connections, !incog.checked);
    });

    // A source is either a chosen file or a typed URL (mutually exclusive). A URL
    // ending in a known video extension is treated as a video, like a video file.
    const VIDEO_URL = /\.(mp4|mov|webm|mkv|avi|m4v|mpe?g)(\?|#|$)/i;
    const urlVal = () => urlEl.value.trim();
    const chosenFile = () => fileEl.files && fileEl.files[0];
    const hasSource = () => !!chosenFile() || urlVal() !== '';
    const isVideoSource = () => {
      const f = chosenFile();
      if (urlVal()) return VIDEO_URL.test(urlVal());
      return !!f && isVideoFile(f);
    };

    // Re-evaluate the action buttons + frame row after any file/URL change. A URL or
    // video can't replace-in-place (nothing to keep the same is fine, but the fetch is
    // async and always a fresh open) — so Replace only lights for a local image file.
    const refresh = () => {
      const has = hasSource();
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
      replaceBtn.disabled = !has || !!urlVal() || isVideoSource() || !canReplace();
      frameRow.style.display = isVideoSource() ? '' : 'none';
    };
    // Typing a URL clears a chosen file and vice-versa (mirrors the desktop dialog).
    fileEl.addEventListener('change', () => { if (chosenFile()) urlEl.value = ''; refresh(); });
    urlEl.addEventListener('input', () => { if (urlVal()) fileEl.value = ''; refresh(); });

    // Fetch a URL's bytes into a File (same-origin / data: / CORS-enabled), mirroring
    // the Links modal's honest fetch path (a canvas readback would taint without CORS).
    const fetchUrlToFile = async (url) => {
      const resp = await fetch(url);
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const blob = await resp.blob();
      const ext = (blob.type.split('/')[1] || url.split(/[?#]/)[0].split('.').pop() || 'png').slice(0, 5);
      const name = (url.split(/[?#]/)[0].split('/').pop() || 'image').replace(/\.[a-z0-9]+$/i, '') + '.' + ext;
      return new File([blob], name, { type: blob.type || 'image/png' });
    };

    // A video source is converted to a captured still frame first (reusing the same
    // capture as the scripting facade); image files pass through unchanged.
    const toFrameIfVideo = async (file) => {
      if (!isVideoFile(file)) return file;
      return await videoFileToImageFile(file, Number(frameEl && frameEl.value) || 0);
    };

    // Resolve the current source (file or URL) to a still-image File, or null on error
    // (already notified). URLs are fetched first, then treated exactly like a file.
    const resolveSource = async () => {
      try {
        const url = urlVal();
        const file = url ? await fetchUrlToFile(url) : chosenFile();
        if (!file) return null;
        return await toFrameIfVideo(file);
      } catch (e) {
        notify(urlVal()
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
      if (!chosenFile() || !canReplace()) return;  // replace is local-image only
      const resolved = await resolveSource();
      if (!resolved) return;
      app.replaceProjectImage(resolved, { rename: renameEl.checked, keepAnnotations: keepEl.checked });
      close();
    });
  }
}
define('stencil-open-image-modal', StencilOpenImageModal);
