// The toolbar's control sections as markup: image/meta/projects/connections/edit, then the
// line, point, draw, view and zoom rows. ui/toolbar.js composes them; every input is wired by id.
import { icon, DRAW_MODE_ICON } from '../icons.js';
import UI_STRINGS from '../../config/uiStrings.json' with { type: 'json' };

export const toolbarImageSectionsHtml = () => `            <!-- ── Section: Image ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Image</div>
                <div class="ctrl-section-row">
                    <!-- One Open entry (empty state). Opens the unified Open dialog: local file, URL, or new blank. -->
                    <button id="load-image-btn" class="btn-icon-text" data-hk-title="loadImage" data-title="Open an image — local file, URL, or new blank">${icon('image')}<span>Open Image</span></button>
                    <!-- Image actions (shown only when an image is loaded). #save-image moved here from Data. -->
                    <span id="image-actions" style="display:none;align-items:center;gap:4px;">
                        <button id="save-image" class="btn-icon" data-hk-title="saveImage" data-title="Download image · Right-click for download options" data-disabled-reason="Load an image to download it">${icon('download')}</button>
                        <button id="copy-image" class="btn-icon" data-hk-title="copyImage" data-title="Copy image to clipboard · Right-click for copy options" data-disabled-reason="Load an image to copy it">${icon('copy')}</button>
                        <button id="share-image" class="btn-icon" data-hk-title="shareImage" data-title="Share image" style="display:none;">${icon('share')}</button>
                        <button id="open-in-btn" class="btn-icon" data-hk-title="openIn" data-title="Open in another app">${icon('monitor')}</button>
                        <button id="open-image-btn" class="btn-icon" data-hk-title="openAnotherImage" data-title="Open another image — local file, URL, or new blank">${icon('external')}</button>
                    </span>
                </div>
            </div>

            <!-- ── Section: Description & attributes (project meta: description, keywords, links) ──
                 All three attach to a SAVED project's meta, so they are gated together on an
                 active non-incognito project (ui/state.js), not on an image. -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Description &amp; attributes</div>
                <div class="ctrl-section-row">
                    <button id="description-btn" class="btn-icon" data-hk-title="openDescription" data-title="Project description" data-disabled-reason="Save the project first to add a description">${icon('description')}</button>
                    <button id="keywords-btn" class="btn-icon" data-hk-title="openKeywords" data-title="Project keywords" data-disabled-reason="Save the project first to add keywords">${icon('keywords')}</button>
                    <button id="links-btn" class="btn-icon" data-hk-title="openLinks" data-title="Source &amp; resource links for the current image" data-disabled-reason="Save the project first to add links">${icon('link')}</button>
                </div>
            </div>

            <!-- ── Section: Projects ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Projects</div>
                <div class="ctrl-section-row">
                    <button id="projects-btn" class="btn-icon" data-hk-title="openProjects" data-title="Projects">${icon('layers')}</button>
                    <button id="save-project-btn" class="btn-icon" data-hk-title="saveProject" data-title="Save Project (.stencil) — image + layout + settings in one file (Shift+click: without theme)" data-disabled-reason="Open an image first">${icon('save')}</button>
                    <button id="open-project-btn" class="btn-icon" data-hk-title="openProject" data-title="Open Project (.stencil)">${icon('folder')}</button>
                    <button id="live-sync-btn" class="btn-icon" data-hk-title="toggleLiveSync" data-title="Live sync this project to its .stencil file (auto-save + watch for changes)" data-disabled-reason="Open or save a .stencil file first" disabled>${icon('refresh-cw')}</button>
                    <button id="delete-project-btn" class="btn-icon" data-hk-title="deleteProject" data-title="Delete the linked .stencil file from disk (the project stays open here)" data-disabled-reason="Open or save a .stencil file first" disabled>${icon('trash')}</button>
                </div>
            </div>

            <!-- ── Section: Connections & chat (servers, AI assistant, voice chat) ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Connections &amp; chat</div>
                <div class="ctrl-section-row">
                    <button id="connect-btn" class="btn-icon" data-hk-title="openServers" data-title="Servers — connect to share &amp; co-edit projects">${icon('server')}</button>
                    <button id="chat-btn" class="btn-icon" data-hk-title="toggleChat" data-title="AI assistant — chat to edit the image">${icon('sparkle')}</button>
                    <button id="voice-chat-btn" class="btn-icon" data-hk-title="toggleVoiceChat" data-title="Voice chat — talk to the assistant hands-free, even with the chat closed" data-disabled-reason="Voice input is not supported in this browser">${icon('mic')}</button>
                </div>
            </div>

            <!-- ── Section: Edit (adjust the current image + undo/redo) ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Edit</div>
                <div class="ctrl-section-row">
                    <select id="image-filter" data-hk-title="cycleFilter" data-title="Image Filter" data-disabled-reason="Load an image to apply a filter">
                        <option value="none">No Filter</option>
                        <option value="bw">B&amp;W</option>
                        <option value="sepia">Sepia</option>
                        <option value="invert">Invert</option>
                        <option value="contour">Contour</option>
                        <option value="custom">Tint</option>
                    </select>
                    <input type="color" id="filter-color" value="#7c3aed" data-title="Tint color" style="display:none;width:36px;height:30px;padding:2px;cursor:pointer;border-radius:4px;">
                    <button id="crop-image" class="btn-icon" data-hk-title="cropImage" data-title="Crop image" data-disabled-reason="Load an image to crop">${icon('crop')}</button>
                    <button id="rotate-left" class="btn-icon" data-hk-title="rotateImageLeft" data-title="Rotate image left" data-disabled-reason="Load an image to rotate">${icon('rotate-ccw')}</button>
                    <button id="rotate-right" class="btn-icon" data-hk-title="rotateImageRight" data-title="Rotate image right" data-disabled-reason="Load an image to rotate">${icon('rotate-cw')}</button>
                    <button id="undo" disabled class="btn-icon" data-hk-title="undo" data-title="Undo" data-disabled-reason="Nothing to undo">${icon('undo')}</button>
                    <button id="redo" disabled class="btn-icon" data-hk-title="redo" data-title="Redo" data-disabled-reason="Nothing to redo">${icon('redo')}</button>
                    <!-- Blank-image fill colour (EDIT action: recolours the current blank, keeps lines).
                         Shown only for blank projects; the swatch is a proper colour rect matching the
                         line-colour picker's proportions. -->
                    <button id="blank-color-btn" class="blank-color-btn" type="button" data-title="Blank background color — recolor this blank image (keeps your lines)">
                        <span id="blank-color-swatch" class="blank-color-swatch"></span>Blank
                    </button>
                    <input id="blank-color-input" type="color" tabindex="-1" aria-hidden="true" style="position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;">
                </div>
            </div>`;

export const toolbarStyleSectionsHtml = () => `            <!-- ── Section: Line style ──
                 Line and point styling are two independent things the user reaches for at
                 different moments, so they get their own captioned sections rather than one
                 mixed row where the two same-yellow swatches and two bare numbers blur
                 together. Within a section the captions can stay short (Color / Thickness ·
                 Color / Size) because the section label carries the noun. Mirrored by the
                 desktop style toolbar (MainWindow.cpp buildStyleToolbar). -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Line</div>
                <div class="ctrl-section-row">
                    <label for="line-color" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Color</label>
                    <input type="color" id="line-color" value="#FFFF00" data-title="Line color">
                    <label for="line-thickness" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Thickness</label>
                    <input type="number" id="line-thickness" value="2" min="1" max="20" data-title="Line thickness" style="width:54px">
                    <select id="line-style" data-title="Line style">
                        <option value="solid">Solid</option>
                        <option value="dashed">Dashed</option>
                        <option value="dotted">Dotted</option>
                    </select>
                </div>
            </div>

            <!-- ── Section: Point style ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Point</div>
                <div class="ctrl-section-row">
                    <label for="point-color" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Color</label>
                    <input type="color" id="point-color" value="#FFFF00" data-title="Point color — new lines">
                    <label for="point-size" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Size</label>
                    <input type="number" id="point-size" value="4" min="1" max="30" data-title="Point size" style="width:54px">
                </div>
            </div>

            <!-- ── Section: Drawing actions ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Draw</div>
                <div class="ctrl-section-row">
                    <button id="draw-toggle" class="btn-icon-text btn-draw-fixed" data-hk-title="startDraw" data-title="Start Drawing" data-disabled-reason="Load an image to start drawing">${icon('play', { size: 13 })}<span>Start</span></button>
                    <button id="draw-mode-toggle" class="btn-icon-text btn-draw-fixed" data-title="Drawing mode: Line (click to switch to Rectangle)" data-disabled-reason="Load an image to switch line / rectangle">${DRAW_MODE_ICON.line}<span>Line</span></button>
                </div>
            </div>

            <!-- ── Section: View ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">View</div>
                <div class="ctrl-section-row">
                    <!-- Compare LEADS the section (desktop twin: MainWindowToolbar.cpp's View
                         cluster), on the row's own gap — the extra air was for two bare words. -->
                    <label for="compare-mode" style="font-weight:normal;font-size:13px;color:var(--text-muted);">Compare</label>
                    <select id="compare-mode" data-hk-title="cycleCompare" data-title="${UI_STRINGS.toolbar.compareTooltip}" data-disabled-reason="Load an image to compare">
                        <option value="none">None</option>
                        <option value="original">Original</option>
                        <option value="vertical">Split ↔</option>
                        <option value="horizontal">Split ↕</option>
                    </select>
                    <label data-hk-title="togglePoints" style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" data-title="Show Points (Alt+P)">
                        <input type="checkbox" id="show-points" checked> Points
                    </label>
                    <label data-hk-title="toggleLines" style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" data-title="Show Lines (Alt+L)">
                        <input type="checkbox" id="show-lines" checked> Lines
                    </label>
                    <button id="clear-all-lines" class="danger btn-icon" data-hk-title="clearAllLines" data-title="Clear All Lines" data-disabled-reason="No lines to clear">${icon('eraser')}</button>
                </div>
            </div>

            <!-- ── Section: Zoom ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Zoom</div>
                <div class="ctrl-section-row">
                    <div class="zoom-controls">
                        <button id="zoom-fit" class="btn-icon" data-hk-title="resetZoom" data-title="Fit to window" data-disabled-reason="Load an image to zoom">${icon('fit')}</button>
                        <button id="zoom-out" class="btn-icon" data-title="Zoom out" data-disabled-reason="Load an image to zoom">${icon('minus')}</button>
                        <button id="zoom-in" class="btn-icon" data-title="Zoom in" data-disabled-reason="Load an image to zoom">${icon('plus')}</button>
                        <!-- Zoom % — type an exact value OR pick a preset from the dropdown that
                             opens on focus/click (custom menu; native datalist on number inputs is
                             unreliable). Presets are populated by wireZoomControls. -->
                        <span class="zoom-input-wrap">
                            <input type="number" id="zoom-input" value="100" min="5" max="3200" autocomplete="off" data-title="Zoom %" data-disabled-reason="Load an image to zoom">
                            <div class="zoom-menu" id="zoom-menu" role="listbox" hidden></div>
                        </span>
                        <span style="font-size:13px;font-weight:bold;color:var(--text-muted)">%</span>
                    </div>
                </div>
            </div>`;
