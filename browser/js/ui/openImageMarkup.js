// Three tabs (Local file / URL link / Blank) over one footer. Built once and reused, so the
// component's onOpen resets every field.
import { icon } from './icons.js';
import MEDIA_TYPES from '../config/mediaTypes.json' with { type: 'json' };

export const openImageModalInner = () => `
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
                    <div class="vs-row"><label>Choose</label><input type="file" id="open-image-file" accept="${MEDIA_TYPES.accept.imageOrVideo}"></div>
                </div>

                <!-- Tab: URL link. Preview is explicit (button / Enter) after validation — not
                     on every keystroke — so a half-typed URL never spins up a fetch. -->
                <div class="oi-panel" id="oi-panel-url" style="display:none">
                    <div class="vs-row vs-field"><label data-title="Load an image or video straight from the web">URL</label><input type="url" id="open-image-url" placeholder="https://… (image or video)"><button id="open-image-url-preview" class="btn-icon-text" type="button" data-title="Load a preview of this URL" disabled>${icon('image', { size: 14 })}<span>Preview</span></button></div>
                </div>

                <!-- Tab: Blank -->
                <div class="oi-panel" id="oi-panel-blank" style="display:none">
                    <div class="vs-section">Fill color</div>
                    <div class="vs-row"><label>Presets</label>
                        <span class="bi-presets">
                            <button id="blank-image-white" class="bi-preset bi-preset-white" type="button" data-title="Fill with white">White</button>
                            <button id="blank-image-black" class="bi-preset bi-preset-black" type="button" data-title="Fill with black">Black</button>
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
                    <label data-title="Capture the frame at this time (seconds)">Frame (s)</label>
                    <input type="number" id="open-image-frame" min="0" step="0.1" value="0" style="width:6rem">
                </div>

                <!-- Crop before opening. Unchecked by default; checked reveals the inline crop
                     editor over the preview (aspect locked to the page, Album/Portrait toggle),
                     matching the standalone Crop modal's model. -->
                <div class="vs-row" id="open-image-crop-row" style="display:none">
                    <label data-title="Crop the image to the page aspect before opening">Crop</label>
                    <span class="oi-crop-opt">
                        <input type="checkbox" id="open-image-crop-toggle">
                        <span class="footer-hint">Trim to the page aspect before opening.</span>
                        <button id="open-image-crop-orientation" class="btn-icon-text" type="button" data-title="Swap album / portrait — flips the crop orientation" style="display:none">${icon('swap', { size: 14 })}<span>Album</span></button>
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
                    <label data-title="Open here locally or create on a connected server">Save to</label>
                    <select id="open-image-target"></select>
                </div>
                <!-- Replace options: only shown on the Local file tab over a replaceable project. -->
                <div class="vs-row" id="open-image-replace-row" style="display:none">
                    <label data-title="Swap this project's image, keeping the same project">Replace</label>
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
