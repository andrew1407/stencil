import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon, DRAW_MODE_ICON } from './icons.js';
import { ACCENTS, DEFAULT_ACCENT, accentHex, normalizeHex } from '../core/accents.js';
import { fillAccentMenu, markSelected } from './accentPicker.js';
import { createModalOpenGesture } from './popover.js';
import { replayWaves, surfaceIn, surfaceOut, wireHoverDust, foldDust, rectCenter,
         motionReduced, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { isTypingTarget, notify, onWindowResize } from '../utils.js';
import { VOICE_STATE_EVENT } from '../llm/voiceModes.js';
import { attachVoiceDust } from './voiceDust.js';
import { pageFormatOptions } from '../core/units.js';
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };
import { subscribe, EVENTS } from '../bus/appBus.js';
import { syncWrappedSeparators } from './toolbarSeparators.js';
import { wireVoiceChatToggle } from './voiceToggle.js';
import { wireLogoColorPicker } from './logoAccent.js';
// ── Component: toolbar (controls-wrapper + all control sections) ──────
// Owns the controls markup and the collapse/hints behavior. The individual
// inputs/buttons are wired by DrawingApp via global ids.
export class StencilToolbar extends StencilElement {
  static inner() {
    return `
            <div class="controls-topbar">
                <!-- The wrap exists for the hover ray layer (animations/iconHover.css): SVG
                     can't host ::before/::after, so the rays live on this span. Clicks and
                     the colour picker stay wired to the .app-logo svg itself. -->
                <span class="app-logo-wrap">
                <svg class="app-logo" viewBox="0 0 64 64" width="32" height="32" role="img" aria-label="Stencil" focusable="false">
                    <rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>
                    <rect class="app-logo-frame" x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke-width="2.5"/>
                    <rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>
                    <polyline points="44,20 32,16 20,24 32,32 44,40 32,48 20,44" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
                    <g fill="#FFFF00" stroke="#000000" stroke-width="1.25">
                        <circle cx="44" cy="20" r="2.6"/><circle cx="32" cy="16" r="2.6"/><circle cx="20" cy="24" r="2.6"/><circle cx="32" cy="32" r="2.6"/><circle cx="44" cy="40" r="2.6"/><circle cx="32" cy="48" r="2.6"/><circle cx="20" cy="44" r="2.6"/>
                    </g>
                </svg>
                <!-- Right-click / Alt+click accent menu: the same preset listbox the Visuals
                     dialog uses (accentPicker.js fills it lazily on first open). -->
                <ul class="accent-dd-menu logo-accent-menu" role="listbox" aria-label="Color theme" hidden></ul>
                </span>
                <button id="toggle-controls" class="btn-icon-text" data-hk-title="toggleControls" data-title="Hide controls">${icon('chevron-up')}<span>Controls</span></button>
                <!-- The field shrink-wraps its content (the input carries a size attribute
                     matching the name — see updateProjectTitle), so everything after the name
                     sits beside it instead of at the end of a fixed 240px slot. -->
                <span class="project-name-field" style="flex:0 1 auto;max-width:280px;min-width:0;display:inline-flex;align-items:center;gap:8px;">
                    <span id="project-remote-badge" class="project-remote-badge" style="display:none;flex:0 0 auto;" data-title="Editing a project stored on a server">${icon('server', { size: 13 })}</span>
                    <input id="project-name-input" type="text" size="10" placeholder="No project" readonly disabled
                        style="flex:0 1 auto;min-width:0;font-size:13px;font-weight:600;background:transparent;border:1px solid transparent;border-radius:6px;padding:3px 8px;">
                    <button id="project-name-edit" class="name-edit-btn name-edit-pencil" type="button" data-hk-title="renameProject" data-title="Rename project" style="display:none;">${icon('pencil', { size: 14 })}</button>
                    <button id="project-name-accept" class="name-edit-btn name-edit-accept" type="button" data-title="Save name (Enter)" style="display:none;">${icon('check', { size: 14 })}</button>
                    <button id="project-name-cancel" class="name-edit-btn name-edit-cancel" type="button" data-title="Cancel (Esc)" style="display:none;">${icon('x', { size: 14 })}</button>
                    <button id="project-color-btn" class="name-edit-btn" type="button" data-title="Project color — paints the project name" style="display:none;">${icon('palette', { size: 14 })}</button>
                    <input id="project-color-input" type="color" tabindex="-1" aria-hidden="true" style="position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;">
                    <!-- Sits INSIDE the name field, right after the name (which sizes to its
                         text), so the "?" reads as belonging to this project rather than
                         floating off in the toolbar. Owns its own hover bubble
                         (.hints-popup), so it opts OUT of the shared floating tooltip. -->
                    <span id="hints-btn" class="hints-btn" data-no-tooltip>
                        ?
                        <span class="hints-popup" id="hints-popup"></span>
                    </span>
                </span>
            </div>
            <div id="controls-body">
        <div class="controls">

            <!-- ── Section: Image ── -->
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

            <div class="ctrl-sep"></div>

            <!-- ── Section: Description & attributes (project meta: description, keywords, links) ──
                 All three attach to a SAVED project's meta, so they are gated together on an
                 active non-incognito project (ui/controlState.js), not on an image. -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Description &amp; attributes</div>
                <div class="ctrl-section-row">
                    <button id="description-btn" class="btn-icon" data-hk-title="openDescription" data-title="Project description" data-disabled-reason="Save the project first to add a description">${icon('description')}</button>
                    <button id="keywords-btn" class="btn-icon" data-hk-title="openKeywords" data-title="Project keywords" data-disabled-reason="Save the project first to add keywords">${icon('keywords')}</button>
                    <button id="links-btn" class="btn-icon" data-hk-title="openLinks" data-title="Source &amp; resource links for the current image" data-disabled-reason="Save the project first to add links">${icon('link')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

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

            <div class="ctrl-sep"></div>

            <!-- ── Section: Connections & chat (servers, AI assistant, voice chat) ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Connections &amp; chat</div>
                <div class="ctrl-section-row">
                    <button id="connect-btn" class="btn-icon" data-hk-title="openServers" data-title="Servers — connect to share &amp; co-edit projects">${icon('server')}</button>
                    <button id="chat-btn" class="btn-icon" data-hk-title="toggleChat" data-title="AI assistant — chat to edit the image">${icon('sparkle')}</button>
                    <button id="voice-chat-btn" class="btn-icon" data-hk-title="toggleVoiceChat" data-title="Voice chat — talk to the assistant hands-free, even with the chat closed" data-disabled-reason="Voice input is not supported in this browser">${icon('mic')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

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
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Line style ──
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

            <div class="ctrl-sep"></div>

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

            <div class="ctrl-sep"></div>

            <!-- ── Section: Drawing actions ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Draw</div>
                <div class="ctrl-section-row">
                    <button id="draw-toggle" class="btn-icon-text btn-draw-fixed" data-hk-title="startDraw" data-title="Start Drawing" data-disabled-reason="Load an image to start drawing">${icon('play', { size: 13 })}<span>Start</span></button>
                    <button id="draw-mode-toggle" class="btn-icon-text btn-draw-fixed" data-title="Drawing mode: Line (click to switch to Rectangle)" data-disabled-reason="Load an image to switch line / rectangle">${DRAW_MODE_ICON.line}<span>Line</span></button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

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

            <div class="ctrl-sep"></div>

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
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Page ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Page</div>
                <div class="ctrl-section-row">
                    <!-- Custom… first, then every named ISO format from PAGE_SIZES with its
                         physical size (re-rendered in the active unit by applyUnitToUI). -->
                    <select id="page-size" data-title="Page size">
                        <option value="custom">Custom…</option>
                        ${pageFormatOptions()}
                    </select>
                    <select id="unit-select" data-title="Display units (cm / inches)">
                        <option value="cm">cm</option>
                        <option value="in">in</option>
                    </select>
                    <span id="custom-size-group" style="display:none;align-items:center;gap:6px;">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">W</label>
                        <input type="number" id="custom-page-width" value="21" min="0.1" max="500" step="0.1" style="width:96px">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">H</label>
                        <input type="number" id="custom-page-height" value="29.7" min="0.1" max="500" step="0.1" style="width:96px">
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Formula ──
                 Its OWN section, not a tail of Page (desktop parity: MainWindowToolbar.cpp
                 builds the same named cluster between PAGE and DATA). The two fields are
                 wide, so inside Page every toggle of the pill resized that section and the
                 whole wrapping row re-flowed around it — the sections after it jumped a row
                 (user report, with a picture). On its own the growth is its own. -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Formula</div>
                <div class="ctrl-section-row">
                    <label class="pill-toggle" data-title="Transform page coordinates with a formula f(x,y)">
                        <input type="checkbox" id="allow-formulas"> 𝑓(x,y)
                    </label>
                    <span id="formula-inputs" style="display:none;align-items:center;gap:6px;">
                        <input type="text" id="formula-x" placeholder="x(x)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <input type="text" id="formula-y" placeholder="y(y)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <span id="formula-error" data-title="Invalid formula" style="color:var(--danger);display:none;">${icon('alert', { size: 15 })}</span>
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Data ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Data</div>
                <div class="ctrl-section-row">
                    <button id="copy-json-btn" class="btn-icon" data-hk-title="copyLayout" data-title="Copy full Layout JSON (lines + all applied edits)" data-disabled-reason="Draw at least one line to copy">${icon('clipboard')}</button>
                    <button id="download-json" class="btn-icon" data-hk-title="downloadJson" data-title="Download Layout JSON" data-disabled-reason="Draw at least one line to export">${icon('file-down')}</button>
                    <input type="file" id="upload-json" accept=".json" style="display:none;">
                    <button id="upload-json-btn" class="btn-icon" data-hk-title="uploadJson" data-title="Upload Layout JSON" data-disabled-reason="Load an image first">${icon('file-up')}</button>
                    <button id="script-btn" class="btn-icon" data-hk-title="openScript" data-title="Stencil script (.stc) — write and run a script over this project" data-disabled-reason="Open an image first">${icon('script')}</button>
                    <button id="clear-storage" class="danger btn-icon" data-hk-title="clearProject" data-title="Remove current project" data-disabled-reason="Open an image first — nothing to remove">${icon('trash')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Settings ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Settings</div>
                <div class="ctrl-section-row">
                    <button id="incognito-toggle" class="btn-icon" data-hk-title="toggleIncognito" data-title="Incognito — edit without saving" data-disabled-reason="Choose incognito before adding an image">${icon('incognito')}</button>
                    <button id="fullscreen-toggle" class="btn-icon" data-hk-title="fullscreen" data-title="Fullscreen">${icon('maximize')}</button>
                    <button id="theme-toggle" class="btn-icon" data-hk-title="toggleTheme" data-title="Toggle dark / light theme">${icon('moon')}</button>
                    <button id="settings-btn" class="btn-icon" data-hk-title="openHotkeys" data-title="Keyboard shortcuts">${icon('gear')}</button>
                    <button id="visuals-btn" class="btn-icon" data-hk-title="openVisuals" data-title="Default visuals &amp; highlight styles">${icon('palette')}</button>
                    <button id="info-btn" class="btn-icon" data-hk-title="openHelp" data-title="Controls &amp; shortcuts help">${icon('help')}</button>
                </div>
            </div>

        </div>
            </div><!-- /controlsBody -->
    `;
  }
  static template() { return hostTag('stencil-toolbar', 'class="controls-wrapper"', StencilToolbar.inner()); }

  wire(_app) {
    const btn = document.getElementById('toggle-controls');
    const body = document.getElementById('controls-body');
    const hintsBtn = document.getElementById('hints-btn');
    const popup = document.getElementById('hints-popup');
    // Shown by a :hover rule alone, so its sand is wired here (js/ui/motion.js).
    wireHoverDust(hintsBtn, popup);
    let hidden = false;

    // Two facts, nothing else: the image size, and — only in incognito — that this
    // session is never saved. Shortcut hints live in the ℹ info modal (infoConfig.json);
    // the incognito line here replaces the floating canvas pill.
    const refresh = () => {
      const el = document.getElementById('image-info');
      // data-size is the info line's OWN text — its incognito tag is a child element,
      // and this bubble states that fact on its own line below.
      const size = el ? (el.dataset.size ?? el.textContent) : '';
      const incognito = document.body.classList.contains('incognito-mode');
      const hasImage = /^Image Size:/.test(size);
      // Shown once an image is open — or, image or not, while incognito is on — and only
      // while the toolbar is COLLAPSED: with the tool rows up the info line already says
      // this; folded away (layout/infoLine.css hides it too), this bubble is the one place left.
      const collapsed = document.body.classList.contains('controls-collapsed');
      const live = (hasImage || incognito) && collapsed;
      hintsBtn.style.display = live ? 'inline-flex' : 'none';
      if (!live) { popup.textContent = ''; return; }
      popup.textContent = hasImage ? size : 'No image loaded';
      popup.classList.toggle('has-incognito', incognito);
      if (incognito) {
        const line = document.createElement('span');
        line.className = 'hints-incognito';
        // Same glyph as the info line and the toolbar toggle — one incognito mark.
        line.innerHTML = `${icon('incognito', { size: 13 })}<span>Incognito — not saved</span>`;
        popup.appendChild(line);
      }
    };

    btn.addEventListener('click', () => {
      hidden = !hidden;
      // The shared fold-with-dust ritual (motion.js foldDust): the tool rows come apart
      // into motes streaming up past the top edge and gather back out of it.
      foldDust(body, body, 'hidden', hidden, 'top',
        { toggle: () => body.classList.toggle('hidden', hidden) });
      // The fold is a body-level state: the info line hides with the rows (CSS), and the
      // "?" badge appears in its place (refresh, via the class observer below).
      document.body.classList.toggle('controls-collapsed', hidden);
      // The glyph is NOT swapped — animations/collapse.css spins the one chevron 180° (up ⇄ down)
      // off `#controls-body.hidden`, so the arrow turns with the fold instead of blinking.
      btn.dataset.title = hidden ? 'Show controls' : 'Hide controls';
      btn.dataset.tip = hotkeys.hkTitle(hidden ? 'Show controls' : 'Hide controls', 'toggleControls');
    });

    // The size line drives the bubble; the incognito class rides on <body>, which the
    // toggle, the chat `incognito` op and an incognito launch/adoption all set.
    new MutationObserver(refresh).observe(document.getElementById('image-info'),
      { childList: true, characterData: true, subtree: true });
    new MutationObserver(refresh).observe(document.body, { attributes: true, attributeFilter: ['class'] });
    refresh();

    wireLogoColorPicker(this.querySelector('.app-logo'), _app);
    wireVoiceChatToggle(this.querySelector('#voice-chat-btn'), _app);
    // The section separators follow the wrap (below): measured again whenever this
    // toolbar, or the window around the fullscreen clone, changes size.
    const syncSeps = () => {
      syncWrappedSeparators(this);
      const fs = document.getElementById('fs-controls-panel');
      if (fs) syncWrappedSeparators(fs);
    };
    // Every SECTION is watched too, not just the toolbar: the f(x,y) fields and the custom
    // page's W/H boxes slide open inside theirs and re-wrap the row while the toolbar's own
    // box never moves.
    if (typeof ResizeObserver === 'function') {
      const ro = new ResizeObserver(syncSeps);
      ro.observe(this);
      for (const sec of this.querySelectorAll('.ctrl-section')) ro.observe(sec);
    }
    onWindowResize(syncSeps);
    subscribe(EVENTS.fullscreenChanged, () => setTimeout(syncSeps, 0));
    syncSeps();
  }
}

// The pieces the toolbar wires but does not own — re-exported, since the suites and
// appContainer.js always found them here.
export { syncWrappedSeparators, WRAPPED_SEP_CLASS } from './toolbarSeparators.js';
export { wireLogoColorPicker } from './logoAccent.js';

define('stencil-toolbar', StencilToolbar);
