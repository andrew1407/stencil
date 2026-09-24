// Three tabs (Local file / URL link / Blank) over one footer. Built once and reused, so the
// component's onOpen resets every field.
import { icon } from '../icons.js';
import '../tabs.js';   // defines <stencil-tabs>, which this markup writes
import { StencilOiFileSource } from './sources/file.js';
import { StencilOiUrlSource } from './sources/url.js';
import { StencilOiBlankTab } from './sources/blank.js';

export const openImageModalInner = () => `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('image', { size: 18 })} Open Image</h2>
                <button class="app-modal-close btn-icon-text" id="open-image-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <!-- Source tabs: pick how to add an image. The strip is the shared element;
                     each panel is the tab that owns it. -->
                <stencil-tabs active-class="is-active" style="display:contents">
                <div class="oi-tabs" role="tablist">
                    <button class="oi-tab is-active" id="oi-tab-file" data-tab="file" role="tab" type="button">${icon('file-text', { size: 14 })}<span>Local file</span></button>
                    <button class="oi-tab" id="oi-tab-url" data-tab="url" role="tab" type="button">${icon('link', { size: 14 })}<span>URL link</span></button>
                    <button class="oi-tab" id="oi-tab-blank" data-tab="blank" role="tab" type="button">${icon('plus-circle', { size: 14 })}<span>Blank</span></button>
                </div>
                </stencil-tabs>
${StencilOiFileSource.template()}
${StencilOiUrlSource.template()}
${StencilOiBlankTab.template()}

                <!-- Live preview of the chosen file/URL source (blank tab has none). A video
                     shows a scrubber to pick the frame. The stage holds BOTH media elements
                     and is the crop box's positioning context: cropping a video draws the
                     rect on the player itself, so nothing is added or taken away. -->
                <div class="oi-status" id="open-image-preview-status"></div>
                <div class="oi-preview" id="open-image-preview" style="display:none">
                    <div id="open-image-crop-stage" style="position:relative;display:none;line-height:0;max-width:100%;background:#222;">
                        <video id="open-image-preview-video" muted playsinline preload="auto" style="display:none;width:auto;height:auto;max-width:100%;max-height:60vh;background:#222;"></video>
                        <img id="open-image-preview-img" alt="Preview" style="display:block;width:auto;height:auto;max-width:100%;max-height:60vh;user-select:none;-webkit-user-drag:none;">
                        <div id="open-image-crop-shade-clip" style="position:absolute;inset:0;overflow:hidden;pointer-events:none;">
                            <div id="open-image-crop-shade" style="position:absolute;box-shadow:0 0 0 9999px rgba(0,0,0,0.45);display:none;"></div>
                        </div>
                        <div id="open-image-crop-box" style="position:absolute;box-sizing:border-box;border:2px solid var(--accent-2);cursor:move;display:none;">
                            <span class="crop-handle" data-corner="0" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;left:-8px;top:-8px;cursor:nwse-resize;"></span>
                            <span class="crop-handle" data-corner="1" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;right:-8px;top:-8px;cursor:nesw-resize;"></span>
                            <span class="crop-handle" data-corner="2" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;right:-8px;bottom:-8px;cursor:nwse-resize;"></span>
                            <span class="crop-handle" data-corner="3" style="position:absolute;width:14px;height:14px;background:var(--accent-2);border:2px solid #fff;border-radius:50%;left:-8px;bottom:-8px;cursor:nesw-resize;"></span>
                        </div>
                    </div>
                    <!-- The frame scrub, a player's own progress bar: the video carries no
                         native controls, so the bar is ours on both surfaces (desktop twin
                         QSlider#oiFrameScrub). JS sizes it to the picture above it. -->
                    <input type="range" id="open-image-frame-scrub" class="oi-scrub" min="0" max="0" value="0" step="1" aria-label="Video frame" style="display:none">
                    <div id="open-image-crop-dims" style="font-size:13px;color:var(--text-muted);display:none;"></div>
                </div>

                <!-- Frame index: shown when the file/URL source is a video (a still frame is captured). -->
                <div class="vs-row" id="open-image-frame-row" style="display:none">
                    <label data-title="Capture this frame (desktop parity; the browser cannot read a video's true rate, so frames count at 30/s)">Frame</label>
                    <input type="number" id="open-image-frame" min="0" step="1" value="0" style="width:6rem">
                </div>

                <!-- Crop before opening. Unchecked by default; checked reveals the inline crop
                     editor over the preview (aspect locked to the page, Album/Portrait toggle),
                     matching the standalone Crop modal's model. -->
                <div class="vs-row" id="open-image-crop-row" style="display:none">
                    <label data-title="Crop the image to the page aspect before opening">Crop</label>
                    <span class="oi-crop-opt">
                        <input type="checkbox" id="open-image-crop-toggle">
                        <label class="footer-hint" for="open-image-crop-toggle">Trim to the page aspect before opening.</label>
                        <button id="open-image-crop-orientation" class="btn-icon-text" type="button" data-title="Swap album / portrait — flips the crop orientation" style="display:none">${icon('swap', { size: 14 })}<span>Album</span></button>
                    </span>
                </div>
                <!-- The crop's own ASPECT RATIO — its own row, shown/hidden with the same
                     particle sweep as the size read-out below the stage (only while cropping;
                     never the PROJECT's own page). A handful of plain ratios beside it: every
                     named ISO page (A/B/C) shares one ratio, so listing all of them here said
                     nothing a single "Page" entry doesn't already say. -->
                <div class="vs-row" id="open-image-crop-size-row" style="display:none">
                    <label data-title="The crop's own aspect ratio">Aspect ratio</label>
                    <span class="oi-crop-size">
                        <select id="open-image-crop-size">
                            <option value="page">Page — Default</option>
                            <option value="1:1">1:1 (Square)</option>
                            <option value="2:3">2:3</option>
                            <option value="custom">Custom</option>
                        </select>
                        <span class="oi-crop-size-custom" id="open-image-crop-size-custom" style="display:none">
                            <span class="oi-crop-size-field">
                                <label for="open-image-crop-size-w">W</label>
                                <input type="number" id="open-image-crop-size-w" min="0.1" max="500" step="0.1">
                            </span>
                            <span class="oi-crop-size-field">
                                <label for="open-image-crop-size-h">H</label>
                                <input type="number" id="open-image-crop-size-h" min="0.1" max="500" step="0.1">
                            </span>
                        </span>
                    </span>
                </div>

                <!-- ── Common options ── -->
                <!-- Incognito applies to a file/URL open; a new blank never supported it
                     (create the blank, then toggle incognito) so the row hides on that tab. -->
                <div class="vs-row" id="open-image-incognito-row">
                    <label>Incognito</label>
                    <span class="oi-incognito">
                        <input type="checkbox" id="open-image-incognito">
                        <label class="footer-hint" for="open-image-incognito">Edit without saving — the image is never written to storage.</label>
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
