import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';

// ── Component: source/resource links modal ──────────────────────
// Opened from the toolbar 🔗 button: view/edit a project's provenance (source image
// URL + originating web page), rename, and add an image BY URL (image or scrubbed
// video frame) through the normal upload path.
// CORS caveat: a cross-origin URL without permissive headers previews but its
// bytes/frame can't be read — load fails with a hint to use extension/desktop.

// Browsers don't expose a video's real fps, so displayed frame indices use this
// assumed fps (frame = time × fps); captured pixels are exact regardless.
const ASSUMED_FPS = 30;

// A best-effort "name.ext" from a URL for the synthetic File handed to the editor.
const filenameFromUrl = (url, fallbackExt = 'png') => {
  try {
    if (url.startsWith('data:')) return `image.${fallbackExt}`;
    const u = new URL(url, location.href);
    const base = decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || '');
    if (base && /\.[a-z0-9]{2,4}$/i.test(base)) return base;
    return `${base || 'image'}.${fallbackExt}`;
  } catch {
    return `image.${fallbackExt}`;
  }
};

export class StencilLinksModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('link', { size: 18 })} Image links</h2>
                <button class="app-modal-close btn-icon-text" id="links-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <!-- Edit the current image's links. Shown only when an image is loaded. -->
                <div id="links-edit-section">
                    <div class="vs-section">Project</div>
                    <div class="vs-row"><label>Name</label><input type="text" id="links-name" placeholder="Untitled"></div>

                    <div class="vs-section">Links</div>
                    <div class="vs-row"><label title="The image/video's own URL">Source</label>
                        <span class="links-field">
                            <input type="text" id="links-source" placeholder="(empty — local upload)">
                            <button id="links-source-open" class="links-open btn-icon" title="Open source in a new tab">${icon('external', { size: 14 })}</button>
                            <button id="links-source-clear" class="links-clear danger btn-icon" title="Remove source link">${icon('x', { size: 14 })}</button>
                        </span>
                    </div>
                    <div class="vs-row"><label title="The web page the image was found on">Resource</label>
                        <span class="links-field">
                            <input type="text" id="links-resource" placeholder="(empty)">
                            <button id="links-resource-open" class="links-open btn-icon" title="Open resource page in a new tab">${icon('external', { size: 14 })}</button>
                            <button id="links-resource-clear" class="links-clear danger btn-icon" title="Remove resource link">${icon('x', { size: 14 })}</button>
                        </span>
                    </div>
                </div>

                <!-- Load a NEW image by URL. Shown only when the editor has no image yet. -->
                <div id="links-add-section">
                    <div class="vs-section">Add image by URL</div>
                    <div class="vs-row"><label title="The image/video's own URL — becomes the source link">Image / video URL</label>
                        <span class="links-field">
                            <input type="text" id="links-url" placeholder="https://… (image or video)">
                            <button id="links-preview" class="links-open btn-icon-text" title="Preview">${icon('eye', { size: 14 })}<span>Preview</span></button>
                        </span>
                    </div>
                    <div class="vs-row"><label>Resource URL</label><input type="text" id="links-url-resource" placeholder="(optional — page the image is on)"></div>
                    <!-- Save target: only shown when at least one server is connected. -->
                    <div class="vs-row" id="links-target-row" style="display:none">
                        <label title="Load locally or create on a connected server">Save to</label>
                        <select id="links-target"></select>
                    </div>
                    <div class="links-preview-wrap" id="links-preview-wrap" style="display:none">
                        <img id="links-preview-img" alt="" style="display:none">
                        <video id="links-preview-video" muted playsinline controls crossorigin="anonymous" style="display:none"></video>
                        <!-- The <video> has its own timeline; we only add an exact frame number
                             (synced with the video's current time). -->
                        <div class="links-frame-row" id="links-frame-row" style="display:none">
                            <label class="links-frame-num" title="Exact frame (browser assumes ~30 fps)">Frame
                                <input type="number" id="links-frame-num" min="0" value="0" step="1">
                                <span id="links-frame-total"></span>
                            </label>
                        </div>
                        <div class="links-preview-hint" id="links-preview-hint"></div>
                    </div>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint" id="links-foot-hint"></span>
                <button id="links-load" disabled class="btn-icon-text">${icon('download')}<span>Load into editor</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-links-modal', 'id="links-modal-overlay" class="app-modal-overlay"', StencilLinksModal.inner()); }

  wire(app) {
    const $ = id => document.getElementById(id);
    const overlay = $('links-modal-overlay');
    const nameEl = $('links-name');
    const sourceEl = $('links-source');
    const resourceEl = $('links-resource');
    const urlEl = $('links-url');
    const urlResourceEl = $('links-url-resource');
    const previewWrap = $('links-preview-wrap');
    const previewImg = $('links-preview-img');
    const previewVideo = $('links-preview-video');
    const previewHint = $('links-preview-hint');
    const frameRow = $('links-frame-row');
    const frameNum = $('links-frame-num');
    const frameTotal = $('links-frame-total');
    const loadBtn = $('links-load');
    const editSection = $('links-edit-section');
    const addSection = $('links-add-section');
    const footHint = $('links-foot-hint');
    const targetEl = $('links-target');
    const targetRow = $('links-target-row');

    // 'none' until a preview succeeds; drives what Load captures.
    let previewKind = 'none';
    let maxFrame = 0;       // last frame index (floor(duration × fps))
    let syncing = false;    // guard slider ↔ number ↔ video echo loops

    const resetPreview = () => {
      previewKind = 'none';
      maxFrame = 0;
      previewWrap.style.display = 'none';
      previewImg.style.display = 'none';
      previewImg.removeAttribute('src');
      previewVideo.style.display = 'none';
      previewVideo.removeAttribute('src');
      previewVideo.load?.();
      frameRow.style.display = 'none';
      frameNum.value = '0';
      frameTotal.textContent = '';
      previewHint.textContent = '';
      loadBtn.disabled = true;
    };

    // ── Video frame number: the <video> has its own timeline slider; this just
    // mirrors the current frame and lets the user jump to an exact one. The video's
    // currentTime is the single source of truth. ──
    const clampFrame = (n) => Math.max(0, Math.min(maxFrame, Math.round(Number(n) || 0)));
    // The number input already shows the frame, so the hint stays generic.
    const videoFrameHint = () => 'Video — scrub it or type a frame number, then Load.';
    // Mirror a frame index into the number input without re-triggering its handler.
    const reflectFrame = (f) => {
      syncing = true;
      frameNum.value = String(f);
      syncing = false;
    };
    // Seek the video to a typed frame. A paused <video> often won't repaint the new
    // frame until a compositor nudge (the "moves only on mouse move" symptom), so we
    // request a video-frame callback to force the sought frame to present immediately.
    const seekToFrame = (n) => {
      const f = clampFrame(n);
      reflectFrame(f);
      if (previewVideo.readyState >= 1 && isFinite(previewVideo.duration)) {
        previewVideo.currentTime = Math.min(previewVideo.duration, f / ASSUMED_FPS);
        previewVideo.requestVideoFrameCallback?.(() => {});  // force the paint
      }
      previewHint.textContent = videoFrameHint(f);
    };
    frameNum.addEventListener('input', () => { if (!syncing) seekToFrame(frameNum.value); });
    frameNum.addEventListener('change', () => { if (!syncing) seekToFrame(frameNum.value); });
    // The video's own slider (or any other seek) reflects back into the number.
    const reflectFromVideo = () => { if (!syncing) reflectFrame(clampFrame(previewVideo.currentTime * ASSUMED_FPS)); };
    previewVideo.addEventListener('seeked', reflectFromVideo);
    previewVideo.addEventListener('timeupdate', reflectFromVideo);

    // Enter in the URL fields triggers Preview (rather than doing nothing / submitting).
    [urlEl, urlResourceEl].forEach(el => el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); $('links-preview').click(); }
    }));

    // Re-read the current project's name/source/resource into the fields. Pulled out
    // of onOpen so it can also refresh LIVE while the modal is open (e.g. when the
    // console's stencil.current.source = … updates the active project).
    const syncLinkFields = () => {
      nameEl.value = (app.activeProjectId && app.storage.store.getMeta(app.activeProjectId)?.name) || app.imageBaseName || '';
      sourceEl.value = app.imageSource || '';
      resourceEl.value = app.imageResource || '';
    };

    const { open, close } = wireModalShell(overlay, $('links-btn'), $('links-close'), {
      onOpen: () => {
        // An image already loaded → only edit its links. No image yet → only the
        // add-by-URL loader (the URL becomes the source; resource is optional).
        const hasImage = !!app.image;
        editSection.style.display = hasImage ? 'block' : 'none';
        addSection.style.display = hasImage ? 'none' : 'block';
        loadBtn.style.display = hasImage ? 'none' : '';
        footHint.textContent = hasImage
          ? 'Editing the current image’s links.'
          : 'Cross-origin URLs need CORS headers to load here; otherwise use the extension or desktop app.';

        syncLinkFields();
        urlEl.value = '';
        urlResourceEl.value = '';
        // Target chooser only relevant for the add-by-URL (no image yet) path.
        if (!hasImage) fillTargetSelect(targetEl, targetRow, app.connections);
        else if (targetRow) targetRow.style.display = 'none';
        resetPreview();
      },
      onClose: resetPreview,
    });

    // Live-refresh the open modal on project-set changes — keeps link fields in sync
    // with on-the-fly source/resource edits. Uses the window event (fired by
    // TabsCoordinator.projectsChanged in THIS tab; onProjectsChanged only fires for
    // OTHER tabs), so same-tab console edits refresh too.
    window.addEventListener('stencil:registry-changed', () => {
      if (overlay.classList.contains('modal-open')) syncLinkFields();
    });

    // ── Current-project links: edit / open / remove ──
    const persist = () => { app.storage.save(); };

    const commitName = () => {
      const v = nameEl.value.trim();
      // renameProject keeps imageBaseName in lockstep for the active project.
      if (app.activeProjectId && v) app.renameProject(app.activeProjectId, v);
    };
    nameEl.addEventListener('change', commitName);

    const bindLinkField = (input, openBtn, clearBtn, key) => {
      input.addEventListener('change', () => {
        app[key] = input.value.trim() || null;
        persist();
      });
      openBtn.addEventListener('click', () => {
        const url = input.value.trim();
        if (!url) {
          notify('No link to open', 'fail');
          return;
        }
        window.open(url, '_blank', 'noopener');
      });
      clearBtn.addEventListener('click', () => {
        input.value = '';
        app[key] = null;
        persist();
        notify('Link removed', 'ok');
      });
    };
    bindLinkField(sourceEl, $('links-source-open'), $('links-source-clear'), 'imageSource');
    bindLinkField(resourceEl, $('links-resource-open'), $('links-resource-clear'), 'imageResource');

    // ── Add image by URL: preview ──
    $('links-preview').addEventListener('click', () => {
      const url = urlEl.value.trim();
      if (!url) {
        notify('Enter an image or video URL first', 'fail');
        return;
      }
      resetPreview();
      previewWrap.style.display = 'block';
      previewHint.textContent = 'Loading…';
      // Try as an image first; on error fall back to a video element.
      previewImg.crossOrigin = 'anonymous';
      previewImg.onload = () => {
        previewKind = 'image';
        previewImg.style.display = 'block';
        previewHint.textContent = `Image ${previewImg.naturalWidth}×${previewImg.naturalHeight}`;
        loadBtn.disabled = false;
      };
      previewImg.onerror = () => {
        previewImg.style.display = 'none';
        // Fall back to a video: let the user scrub to the frame they want, via the
        // slider / frame number (and the native controls), all kept in sync.
        previewVideo.onloadeddata = () => {
          previewKind = 'video';
          previewVideo.style.display = 'block';
          const dur = isFinite(previewVideo.duration) ? previewVideo.duration : 0;
          maxFrame = Math.max(0, Math.floor(dur * ASSUMED_FPS));
          frameNum.max = String(maxFrame);
          frameTotal.textContent = maxFrame ? `/ ${maxFrame}` : '';
          reflectFrame(0);
          frameRow.style.display = 'flex';
          previewHint.textContent = videoFrameHint(0);
          loadBtn.disabled = false;
        };
        previewVideo.onerror = () => {
          previewHint.textContent = 'Could not load that URL as an image or video.';
          loadBtn.disabled = true;
        };
        previewVideo.src = url;
        previewVideo.load();
      };
      previewImg.src = url;
    });

    // ── Add image by URL: load the previewed image/frame into the editor ──
    const loadIntoEditor = async () => {
      const url = urlEl.value.trim();
      const resource = urlResourceEl.value.trim() || null;
      try {
        let file;
        if (previewKind === 'video') {
          const f = clampFrame(previewVideo.currentTime * ASSUMED_FPS);
          const cnv = document.createElement('canvas');
          cnv.width = previewVideo.videoWidth;
          cnv.height = previewVideo.videoHeight;
          cnv.getContext('2d').drawImage(previewVideo, 0, 0);
          const blob = await new Promise((res, rej) =>
            cnv.toBlob(b => b ? res(b) : rej(new Error('frame capture failed')), 'image/png'));
          file = new File([blob], filenameFromUrl(url, 'png').replace(/\.[a-z0-9]+$/i, '') + `-frame${f}.png`, { type: 'image/png' });
        } else {
          // Image: fetch the bytes (same-origin / data: / CORS-enabled). A canvas
          // readback of the preview <img> would also taint without CORS, so fetch
          // is the honest path — its failure is the CORS limitation we warn about.
          const resp = await fetch(url);
          if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
          const blob = await resp.blob();
          file = new File([blob], filenameFromUrl(url, (blob.type.split('/')[1] || 'png')), { type: blob.type || 'image/png' });
        }
        const address = (targetEl && targetEl.value) || undefined;
        app.loadImageFromFile(file, { source: url, resource, address });
        close();
        notify('Image loaded from URL', 'ok');
      } catch (err) {
        notify(`Could not load that URL — ${err.message}. Cross-origin URLs need CORS headers; try the extension or desktop app.`, 'fail');
      }
    };
    loadBtn.addEventListener('click', loadIntoEditor);
  }
}
define('stencil-links-modal', StencilLinksModal);
