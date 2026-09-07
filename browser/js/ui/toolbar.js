import { StencilElement, hostTag, define } from './base.js';
import { DRAW_MODE_ICON } from '../core/drawingApp.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { ACCENTS, DEFAULT_ACCENT, accentHex, normalizeHex } from '../core/accents.js';
import { fillAccentMenu, markSelected } from './accentPicker.js';
import { createModalOpenGesture } from './popover.js';
import { replayWaves, surfaceIn, surfaceOut, wireHoverDust, foldDust, rectCenter,
         motionReduced, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { isTypingTarget, notify } from '../utils.js';
import { VOICE_STATE_EVENT } from '../llm/voiceModes.js';
import { attachVoiceDust } from './voiceDust.js';
import { pageFormatOptions } from '../core/units.js';
// ── Component: toolbar (controls-wrapper + all control sections) ──────
// Owns the controls markup and the collapse/hints behavior. The individual
// inputs/buttons are wired by DrawingApp via global ids.
export class StencilToolbar extends StencilElement {
  static inner() {
    return `
            <div class="controls-topbar">
                <!-- The wrap exists for the hover ray layer (animations.css): SVG elements
                     can't host ::before/::after, so the rays live on this span. Clicks and
                     the colour picker stay wired to the .app-logo svg itself. -->
                <span class="app-logo-wrap">
                <svg class="app-logo" viewBox="0 0 64 64" width="24" height="24" role="img" aria-label="Stencil" focusable="false">
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
                <button id="toggle-controls" class="btn-icon-text" data-hk-title="toggleControls" data-title="Hide controls" title="Hide controls">${icon('chevron-up')}<span>Controls</span></button>
                <!-- The field shrink-wraps its content (the input carries a size attribute
                     matching the name — see updateProjectTitle), so everything after the name
                     sits beside it instead of at the end of a fixed 240px slot. -->
                <span class="project-name-field" style="flex:0 1 auto;max-width:280px;min-width:0;display:inline-flex;align-items:center;gap:4px;">
                    <span id="project-remote-badge" class="project-remote-badge" style="display:none;flex:0 0 auto;" title="Editing a project stored on a server">${icon('server', { size: 13 })}</span>
                    <input id="project-name-input" type="text" size="10" placeholder="No project" readonly disabled
                        style="flex:0 1 auto;min-width:0;font-size:13px;font-weight:600;background:transparent;border:1px solid transparent;border-radius:6px;padding:3px 8px;">
                    <button id="project-name-edit" class="name-edit-btn name-edit-pencil" type="button" data-hk-title="renameProject" data-title="Rename project" title="Rename project" style="display:none;">${icon('pencil', { size: 13 })}</button>
                    <button id="project-name-accept" class="name-edit-btn name-edit-accept" type="button" title="Save name" style="display:none;">${icon('check', { size: 14 })}</button>
                    <button id="project-name-cancel" class="name-edit-btn name-edit-cancel" type="button" title="Cancel" style="display:none;">${icon('x', { size: 14 })}</button>
                    <button id="project-color-btn" class="name-edit-btn" type="button" title="Project color — paints the project name" style="display:none;">${icon('palette', { size: 14 })}</button>
                    <input id="project-color-input" type="color" tabindex="-1" aria-hidden="true" style="position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;">
                    <!-- Sits INSIDE the name field, right after the name (which sizes to its
                         text), so the "?" reads as belonging to this project rather than
                         floating off in the toolbar. Owns its own hover bubble
                         (.hints-popup), so it opts OUT of the shared floating tooltip. -->
                    <span id="hints-btn" data-no-tooltip style="display:none;flex:0 0 auto;position:relative;cursor:default;font-size:12px;color:var(--text-muted);border:1px solid var(--border-main);border-radius:12px;padding:2px 8px;user-select:none;">
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
                    <button id="load-image-btn" class="btn-icon-text" data-hk-title="loadImage" data-title="Open an image — local file, URL, or new blank" title="Open an image — local file, URL, or new blank">${icon('image')}<span>Open Image</span></button>
                    <!-- Image actions (shown only when an image is loaded). #save-image moved here from Data. -->
                    <span id="image-actions" style="display:none;align-items:center;gap:4px;">
                        <button id="save-image" class="btn-icon" data-hk-title="saveImage" data-title="Download image · Right-click for download options" data-disabled-reason="Load an image to download it" title="Download image — right-click for options">${icon('download')}</button>
                        <button id="copy-image" class="btn-icon" data-hk-title="copyImage" data-title="Copy image to clipboard · Right-click for copy options" data-disabled-reason="Load an image to copy it" title="Copy image to clipboard — right-click for options">${icon('copy')}</button>
                        <button id="share-image" class="btn-icon" data-hk-title="shareImage" data-title="Share image" title="Share image" style="display:none;">${icon('share')}</button>
                        <button id="open-in-btn" class="btn-icon" data-hk-title="openIn" data-title="Open in another app" title="Open in another app">${icon('monitor')}</button>
                        <button id="open-image-btn" class="btn-icon" data-hk-title="openAnotherImage" data-title="Open another image — local file, URL, or new blank" title="Open another image — local file, URL, or new blank">${icon('external')}</button>
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Projects ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Projects</div>
                <div class="ctrl-section-row">
                    <button id="projects-btn" class="btn-icon" data-hk-title="openProjects" data-title="Projects" title="Projects">${icon('layers')}</button>
                    <button id="save-project-btn" class="btn-icon" data-hk-title="saveProject" data-title="Save Project (.stencil) — image + layout + settings in one file (Shift+click: without theme)" data-disabled-reason="Open an image first" title="Save Project (.stencil) — Shift+click to save without the theme">${icon('save')}</button>
                    <button id="open-project-btn" class="btn-icon" data-hk-title="openProject" data-title="Open Project (.stencil)" title="Open Project (.stencil)">${icon('folder')}</button>
                    <button id="live-sync-btn" class="btn-icon" data-hk-title="toggleLiveSync" data-title="Live sync this project to its .stencil file (auto-save + watch for changes)" title="Live sync to file" disabled>${icon('refresh-cw')}</button>
                    <button id="delete-project-btn" class="btn-icon" data-hk-title="deleteProject" data-title="Delete the linked .stencil file from disk (the project stays open here)" data-disabled-reason="Open or save a .stencil file first" title="Delete linked .stencil file" disabled>${icon('trash')}</button>
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
                    <select id="image-filter" data-hk-title="cycleFilter" data-title="Image Filter" data-disabled-reason="Load an image to apply a filter" title="Image Filter">
                        <option value="none">No Filter</option>
                        <option value="bw">B&amp;W</option>
                        <option value="sepia">Sepia</option>
                        <option value="invert">Invert</option>
                        <option value="contour">Contour</option>
                        <option value="custom">Tint</option>
                    </select>
                    <input type="color" id="filter-color" value="#7c3aed" title="Tint color" style="display:none;width:36px;height:30px;padding:2px;cursor:pointer;border-radius:4px;">
                    <button id="crop-image" class="btn-icon" data-hk-title="cropImage" data-title="Crop image" data-disabled-reason="Load an image to crop" title="Crop image — pick the page-shaped region to show on the canvas">${icon('crop')}</button>
                    <button id="rotate-left" class="btn-icon" data-hk-title="rotateImageLeft" data-title="Rotate image left" data-disabled-reason="Load an image to rotate" title="Rotate image left">${icon('rotate-ccw')}</button>
                    <button id="rotate-right" class="btn-icon" data-hk-title="rotateImageRight" data-title="Rotate image right" data-disabled-reason="Load an image to rotate" title="Rotate image right">${icon('rotate-cw')}</button>
                    <button id="undo" disabled class="btn-icon" data-hk-title="undo" data-title="Undo" data-disabled-reason="Nothing to undo" title="Undo">${icon('undo')}</button>
                    <button id="redo" disabled class="btn-icon" data-hk-title="redo" data-title="Redo" data-disabled-reason="Nothing to redo" title="Redo">${icon('redo')}</button>
                    <!-- Blank-image fill colour (EDIT action: recolours the current blank, keeps lines).
                         Shown only for blank projects; the swatch is a proper colour rect matching the
                         line-colour picker's proportions. -->
                    <button id="blank-color-btn" type="button" title="Blank background color — recolor this blank image (keeps your lines)" style="display:none;align-items:center;gap:7px;font-size:12px;color:var(--text-muted);background:var(--bg-info);padding:5px 9px;border-radius:4px;border:1px solid var(--border-main);white-space:nowrap;cursor:pointer;">
                        <span id="blank-color-swatch" style="width:30px;height:22px;border-radius:3px;border:1px solid var(--border-main);display:inline-block;flex:0 0 auto;"></span>Blank
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
                 desktop style toolbar (mainWindow.cpp buildStyleToolbar). -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Line</div>
                <div class="ctrl-section-row">
                    <label for="line-color" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Color</label>
                    <input type="color" id="line-color" value="#FFFF00" title="Line color">
                    <label for="line-thickness" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Thickness</label>
                    <input type="number" id="line-thickness" value="2" min="1" max="20" title="Line thickness" style="width:54px">
                    <select id="line-style" title="Line style">
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
                    <input type="color" id="point-color" value="#FFFF00" title="Point color — new lines">
                    <label for="point-size" style="font-weight:normal;font-size:12px;color:var(--text-muted);">Size</label>
                    <input type="number" id="point-size" value="4" min="1" max="30" title="Point size" style="width:54px">
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Drawing actions ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Draw</div>
                <div class="ctrl-section-row">
                    <button id="draw-toggle" class="btn-icon-text btn-draw-fixed" data-hk-title="startDraw" data-title="Start Drawing" data-disabled-reason="Load an image to start drawing" title="Start Drawing">${icon('play', { size: 13 })}<span>Start</span></button>
                    <button id="draw-mode-toggle" class="btn-icon-text btn-draw-fixed" data-title="Drawing mode: Line (click to switch to Rectangle)" data-disabled-reason="Load an image to switch line / rectangle" title="Drawing mode: Line (click to switch to Rectangle)">${DRAW_MODE_ICON.line}<span>Line</span></button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: View ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">View</div>
                <div class="ctrl-section-row">
                    <label data-hk-title="togglePoints" style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" title="Show Points (Alt+P)">
                        <input type="checkbox" id="show-points" checked> Points
                    </label>
                    <label data-hk-title="toggleLines" style="font-weight:normal;font-size:13px;cursor:pointer;display:flex;align-items:center;gap:4px;" title="Show Lines (Alt+L)">
                        <input type="checkbox" id="show-lines" checked> Lines
                    </label>
                    <!-- Extra left margin, none on the right: the row's flat 8px gap left "Lines"
                         and "Compare" reading as one run of text. The label belongs to the select,
                         so the air goes on the side that separates it from the toggles. -->
                    <label for="compare-mode" style="font-weight:normal;font-size:13px;color:var(--text-muted);margin-left:12px;">Compare</label>
                    <select id="compare-mode" data-hk-title="cycleCompare" data-title="Compare with original&#10;• None — normal editing&#10;• Original — the original only (crop + rotation)&#10;• Vertical split — original left, edit right&#10;• Horizontal split — original top, edit bottom&#10;(hold Alt+Shift+O to peek)" data-disabled-reason="Load an image to compare" title="Compare with original&#10;• None — normal editing&#10;• Original — the original only (crop + rotation)&#10;• Vertical split — original left, edit right&#10;• Horizontal split — original top, edit bottom&#10;(hold Alt+Shift+O to peek)">
                        <option value="none">None</option>
                        <option value="original">Original</option>
                        <option value="vertical">Split ↔</option>
                        <option value="horizontal">Split ↕</option>
                    </select>
                    <button id="clear-all-lines" class="danger btn-icon" data-hk-title="clearAllLines" data-title="Clear All Lines" data-disabled-reason="No lines to clear" title="Clear All Lines">${icon('eraser')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Zoom ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Zoom</div>
                <div class="ctrl-section-row">
                    <div class="zoom-controls">
                        <button id="zoom-out" class="btn-icon" data-title="Zoom out" data-disabled-reason="Load an image to zoom" title="Zoom out">${icon('minus')}</button>
                        <button id="zoom-in" class="btn-icon" data-title="Zoom in" data-disabled-reason="Load an image to zoom" title="Zoom in">${icon('plus')}</button>
                        <!-- Zoom % — type an exact value OR pick a preset from the dropdown that
                             opens on focus/click (custom menu; native datalist on number inputs is
                             unreliable). Presets are populated by wireZoomControls. -->
                        <span class="zoom-input-wrap">
                            <input type="number" id="zoom-input" value="100" min="5" max="3200" autocomplete="off" data-title="Zoom %" data-disabled-reason="Load an image to zoom" title="Zoom % — type an exact value or pick a preset">
                            <div class="zoom-menu" id="zoom-menu" role="listbox" hidden></div>
                        </span>
                        <span style="font-size:13px;font-weight:bold;color:var(--text-muted)">%</span>
                        <button id="zoom-fit" class="btn-icon" data-hk-title="resetZoom" data-title="Fit to window" data-disabled-reason="Load an image to zoom" title="Fit to window">${icon('fit')}</button>
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
                    <select id="page-size" title="Page size">
                        <option value="custom">Custom…</option>
                        ${pageFormatOptions()}
                    </select>
                    <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">Units:</label>
                    <select id="unit-select" title="Display units (cm / inches)">
                        <option value="cm">cm</option>
                        <option value="in">in</option>
                    </select>
                    <span id="custom-size-group" style="display:none;align-items:center;gap:6px;">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">W</label>
                        <input type="number" id="custom-page-width" value="21" min="0.1" max="500" step="0.1" style="width:96px">
                        <label style="font-weight:normal;font-size:12px;color:var(--text-muted);">H</label>
                        <input type="number" id="custom-page-height" value="29.7" min="0.1" max="500" step="0.1" style="width:96px">
                        <span id="custom-unit-label" style="font-size:12px;color:var(--text-muted);">cm</span>
                    </span>
                    <label class="pill-toggle" style="margin-left:6px;" title="Transform page coordinates with a formula f(x,y)">
                        <input type="checkbox" id="allow-formulas"> 𝑓(x,y)
                    </label>
                    <span id="formula-inputs" style="display:none;align-items:center;gap:6px;">
                        <input type="text" id="formula-x" placeholder="x(x)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <input type="text" id="formula-y" placeholder="y(y)=" style="width:180px;font-family:monospace;font-size:12px;">
                        <span id="formula-error" title="Invalid formula" style="color:var(--danger);display:none;">${icon('alert', { size: 15 })}</span>
                    </span>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Data ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Data</div>
                <div class="ctrl-section-row">
                    <button id="download-json" class="btn-icon" data-hk-title="downloadJson" data-title="Download Layout JSON" data-disabled-reason="Draw at least one line to export" title="Download Layout JSON">${icon('download')}</button>
                    <button id="copy-json-btn" class="btn-icon" data-hk-title="copyLayout" data-title="Copy full Layout JSON (lines + all applied edits)" data-disabled-reason="Draw at least one line to copy" title="Copy full Layout JSON (lines + all applied edits)">${icon('copy')}</button>
                    <input type="file" id="upload-json" accept=".json" style="display:none;">
                    <button id="upload-json-btn" class="btn-icon" data-hk-title="uploadJson" data-title="Upload Layout JSON" data-disabled-reason="Load an image first" title="Upload Layout JSON">${icon('upload')}</button>
                    <button id="clear-storage" class="danger btn-icon" data-hk-title="clearProject" data-title="Remove" data-disabled-reason="Open an image first — nothing to remove" title="Remove">${icon('trash')}</button>
                </div>
            </div>

            <div class="ctrl-sep"></div>

            <!-- ── Section: Settings ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Settings</div>
                <div class="ctrl-section-row">
                    <button id="theme-toggle" class="btn-icon" data-hk-title="toggleTheme" data-title="Toggle dark / light theme" title="Toggle dark / light theme">${icon('moon')}</button>
                    <button id="fullscreen-toggle" class="btn-icon" data-hk-title="fullscreen" data-title="Fullscreen" title="Fullscreen">${icon('maximize')}</button>
                    <button id="incognito-toggle" class="btn-icon" data-hk-title="toggleIncognito" data-title="Incognito — edit without saving (choose before adding an image)" title="Incognito — edit without saving (choose before adding an image)">${icon('incognito')}</button>
                    <button id="settings-btn" class="btn-icon" data-hk-title="openHotkeys" data-title="Keyboard shortcuts" title="Keyboard shortcuts">${icon('gear')}</button>
                    <button id="visuals-btn" class="btn-icon" data-hk-title="openVisuals" data-title="Default visuals &amp; highlight styles" title="Default visuals &amp; highlight styles">${icon('palette')}</button>
                    <button id="info-btn" class="btn-icon" data-hk-title="openHelp" data-title="Controls &amp; shortcuts help" title="Controls &amp; shortcuts help">${icon('help')}</button>
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
      // this; folded away (layout.css hides it too), this bubble is the one place left.
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
      // The glyph is NOT swapped — animations.css spins the one chevron 180° (up ⇄ down)
      // off `#controls-body.hidden`, so the arrow turns with the fold instead of blinking.
      btn.dataset.title = hidden ? 'Show controls' : 'Hide controls';
      btn.title = hotkeys.hkTitle(hidden ? 'Show controls' : 'Hide controls', 'toggleControls');
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
    window.addEventListener('resize', syncSeps);
    window.addEventListener('stencil:fullscreen-changed', () => setTimeout(syncSeps, 0));
    syncSeps();
  }
}

// The hairlines between toolbar sections (.ctrl-sep) live in a wrapping flex row, so a
// narrowing window can land one at the START of a row — a stray line shoving that section
// right. A separator whose two neighbours sit on different rows is hidden.
export const WRAPPED_SEP_CLASS = 'ctrl-sep-wrapped';
// Hiding one frees its width, which can pull the next section back up — so one pass
// leaves answers the new layout no longer matches. Re-ask until the set stops moving; a
// width that oscillates stops at the cap, hidden (a missing hairline beats a stray one).
export const SEP_SETTLE_PASSES = 4;
export function syncWrappedSeparators(root, passes = SEP_SETTLE_PASSES) {
  const seps = [...(root?.querySelectorAll?.('.ctrl-sep') || [])];
  // Every separator shown first, so a given width always resolves the same way and the
  // observer that re-runs this never chases its own change.
  for (const sep of seps) sep.classList.remove(WRAPPED_SEP_CLASS);
  const top = (el) => Math.round(el.getBoundingClientRect().top);
  const straddles = (sep) => {
    const prev = sep.previousElementSibling;
    const next = sep.nextElementSibling;
    return !!prev && !!next && top(next) > top(prev);
  };
  for (let pass = 0; pass < passes; pass++) {
    let moved = false;
    for (const sep of seps) {
      const want = straddles(sep);
      if (want === sep.classList.contains(WRAPPED_SEP_CLASS)) continue;
      sep.classList.toggle(WRAPPED_SEP_CLASS, want);
      moved = true;
    }
    if (!moved) return;   // settled: every hairline agrees with the row it is in
  }
  for (const sep of seps) if (straddles(sep)) sep.classList.add(WRAPPED_SEP_CLASS);   // never settled
}

// The hands-free voice chat toggle (js/llm/voiceModes.js): `--voice-level` on <html>
// carries the live loudness (css/animations.css sizes the mics' shine from it), and
// .active marks this button while voice chat is on — mirrored onto the fullscreen
// toolbar clone like the chat button's own state. The LOGO is not a wearer: its shine
// is its own hover (and its accent popover's), never the microphone's (user report).
export function wireVoiceChatToggle(btn, app) {
  if (!btn || !app) return;
  const voice = () => app.voice;
  const buttons = () => document.querySelectorAll('#voice-chat-btn');
  let wasOn = false;
  const sync = () => {
    const v = voice();
    const on = !!v?.voiceChat;
    for (const el of buttons()) {
      el.classList.toggle('active', on);
      if (on !== wasOn) replayWaves(el, on);   // the waves swell in / fly out
    }
    wasOn = on;
  };
  if (!voice()?.supported) btn.disabled = true;   // the disabled-reason tooltip says why
  // Motes leave the tile with the voice while it listens (ui/voiceDust.js).
  attachVoiceDust(btn, () => btn.classList.contains('active'));
  btn.addEventListener('click', () => {
    const v = voice();
    if (!v) return;
    try { v.voiceChat = !v.voiceChat; } catch (err) { notify(err?.message || String(err), 'fail'); }
    sync();
  });
  voice()?.onLevel((level) => {
    document.documentElement.style.setProperty('--voice-level', level.toFixed(3));
  });
  window.addEventListener(VOICE_STATE_EVENT, sync);
  sync();
}

// Double-click (or double-tap) the logo opens a native colour picker that tints THIS
// page's accent only — not saved, not synced, gone on reload; a Visuals preset clears
// it (DrawingApp#applyAccent). Exported for tests/logoAccentMenu.test.js.
export function wireLogoColorPicker(logo, app) {
  if (!logo || !app) return;
  logo.style.cursor = 'pointer';
  const wrap = logo.closest?.('.app-logo-wrap') || logo;

  // ── Hover latch (.logo-hover) ── the pulse/ray loop (animations.css) keys on this
  // class, NOT :hover: the browser force-drops page hover for the whole accent/theme
  // view transition. themeSwap raises `theme-instant` on <html> for exactly that
  // window — hold the latch through it, then trust real :hover once the swap ends.
  const setHover = (on) => wrap.classList?.toggle('logo-hover', on);
  wrap.addEventListener('pointerenter', () => setHover(true));
  wrap.addEventListener('pointerleave', () => {
    const root = document.documentElement;
    // The latch is ANIMATION state only — the menu's peek lifetime is the gesture
    // machine's business (glide/altRelease/linger below, same as every toolbar icon).
    if (!root.classList?.contains('theme-instant')) { setHover(false); return; }
    const settle = () => {
      if (root.classList.contains('theme-instant')) { setTimeout(settle, 60); return; }
      // one beat for the browser to re-establish real hover, then trust it
      setTimeout(() => { if (!wrap.matches(':hover')) setHover(false); }, 90);
    };
    settle();
  });

  // ── Right-click (or Alt+click) → accent preset menu ── the same listbox the Visuals
  // dialog uses (accentPicker.js; custom colours stay on the double-click picker).
  // Non-modal — it only borrows the shared popup motion. Selecting applies through the
  // same setAccent path as the click-cycle, with the logo as the swap origin.
  const menu = wrap.querySelector?.('.logo-accent-menu');
  let menuCloseTimer = null;
  let menuCloseDone = null;
  // How the menu is open right now: 'peek' (Alt-opened) or 'sticky' (right-click).
  // Only the Alt+click no-op below needs the distinction; the LIFETIME rules live in
  // the shared gesture machine.
  let menuKind = null;
  let pendingKind = null;   // set around a machine call so openMenu knows who opened it
  const menuShowing = () => !!menu && !menu.hidden && !menu.classList.contains('dd-closing');
  // Mid accent/theme swap the browser force-drops page :hover (`theme-instant` marks
  // the window — see the hover latch). Pointer-driven dismissal must tell those
  // synthetic leaves from a real one: every swap happens with the menu under the pointer.
  const swapping = () => !!document.documentElement?.classList?.contains('theme-instant');
  const reducedMotion = () =>
    typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onMenuKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  // The list is sand, like every other surface (js/ui/motion.js): it forms from motes
  // streaming out of the logo and comes apart into motes pouring back into it, on the
  // shared menu clock. The `hidden` / `.dd-closing` hooks are unchanged — the dust
  // simply replaces the scale those two used to drive.
  const MENU_IN_MS = SURFACE_MENU_IN_MS;
  const MENU_OUT_MS = SURFACE_MENU_OUT_MS;
  const logoPoint = () => rectCenter(wrap);
  const dustMenu = (enter) => {
    if (!menu) return;
    const point = reducedMotion() ? null : logoPoint();
    (enter ? surfaceIn : surfaceOut)(menu, point, { ms: enter ? MENU_IN_MS : MENU_OUT_MS });
  };
  const openMenu = () => {
    if (!menu) return;
    if (pendingKind) menuKind = pendingKind;
    if (!menu.childElementCount) {
      // A pick APPLIES and leaves the menu up so colours can be tried in a row; rows
      // are built once, keeping scroll + DOM across the swap. themeSwap writes on a
      // LATER beat — app.accent is still the old preset, so mark the key we picked.
      fillAccentMenu(menu, (key) => { app.setAccent(key, logo); markSelected(menu, key); });
    }
    markSelected(menu, app.customAccent ? null : app.accent);
    // Size to CONTENT by default (components.css lifts the shared 280px cap for this
    // copy); cap at the viewport space under the logo so only a genuinely too-short
    // window makes the list scroll (overflow-y:auto shows a scrollbar only then).
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number') {
      menu.style.maxHeight = `${Math.max(90, window.innerHeight - r.bottom - 18)}px`;
    }
    // Reopening mid-close: abort the exit (its animationend must not hide the fresh menu).
    clearTimeout(menuCloseTimer);
    if (menuCloseDone) menu.removeEventListener('animationend', menuCloseDone);
    menu.classList.remove('dd-closing');
    menu.hidden = false;
    dustMenu(true);
    // Idempotent (same refs), so a reopen can't double-register.
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onMenuKey);
  };
  const closeMenu = () => {
    if (!menu || menu.hidden || menu.classList.contains('dd-closing')) return;
    menuKind = null;
    // However it closes, the machine must not keep believing a popover shows — a
    // leaked mode would let a later Alt glide "close" a menu that is already gone.
    g.notifyClosed();
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onMenuKey);
    menuCloseDone = (e) => {
      // animationend BUBBLES: every row runs the hover shimmer on its ::after and the
      // pointer is always over a row at close — an unfiltered listener ended the exit
      // on the first shimmer. Only the menu's OWN animation (or the fallback timer /
      // reduced motion, which pass no event) counts.
      if (e && e.target !== menu) return;
      clearTimeout(menuCloseTimer);
      menu.removeEventListener('animationend', menuCloseDone);
      menu.hidden = true;
      menu.classList.remove('dd-closing');
    };
    // Reduced motion: animations.css neutralises both the rise and the pop-out, so
    // there is no exit to wait for — hide outright rather than sit through the fallback.
    if (reducedMotion()) { menuCloseDone(); return; }
    // Leaves on the shared pop-out; hidden only once the exit has played — with a timer
    // fallback so a missing/neutralised animation can never wedge the menu open.
    menu.classList.add('dd-closing');
    dustMenu(false);
    menuCloseTimer = setTimeout(menuCloseDone, 250);
    menu.addEventListener('animationend', menuCloseDone);
  };
  // ── The shared toolbar peek system (ui/popover.js) ── the accent menu is a MACHINE
  // IN THE GLIDE REGISTRY, exactly like every modal icon's mini window: Alt+hover
  // peeks it (first closing other minis), Alt released over it lingers, elsewhere
  // closes; a right-click open is 'sticky' but a glide still closes it. Modal gating
  // rides the system's own live :hover checks, untouched.
  const g = createModalOpenGesture({
    openFull: () => {},          // the logo opens no full modal — click cycles the accent
    openPopover: () => openMenu(),
    closePopover: () => closeMenu(),
    isPopoverOpen: menuShowing,
    // Engaged at release time = the pointer rests inside the menu (peek → linger).
    isPeekEngaged: () => !!menu?.matches?.(':hover'),
  });
  const altPeek = () => { pendingKind = 'peek'; g.altHover(); pendingKind = null; };
  wrap.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    pendingKind = 'sticky'; g.contextmenu(); pendingKind = null;
  });
  // Alt + hover, both orders (mirrors popover.js wireModalOpenGestures): gliding on
  // with Alt held, and pressing Alt while resting on it. Only the KEY route defers to
  // a focused text control; preventDefault keeps bare Alt off the browser's menu bar.
  wrap.addEventListener('mouseenter', (e) => { if (e.altKey) altPeek(); });
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Alt' || !wrap.matches?.(':hover')) return;
    if (isTypingTarget(document.activeElement)) return;
    e.preventDefault?.();
    altPeek();
  });
  document.addEventListener('keyup', (e) => { if (e.key === 'Alt') g.altRelease(); });
  if (typeof window !== 'undefined' && window.addEventListener) {
    window.addEventListener('blur', () => g.altRelease());
  }
  // The pointer crossing the menu edge drives the linger close — but a swap's synthetic
  // leave lands on a LINGERING menu every time a colour is picked. Hold the decision
  // until the swap ends and then trust real :hover, exactly like the hover latch.
  menu?.addEventListener('mouseenter', () => { if (!swapping()) g.boxEnter(); });
  menu?.addEventListener('mouseleave', () => {
    if (!swapping()) { g.boxLeave(); return; }
    const settle = () => {
      if (swapping()) { setTimeout(settle, 60); return; }
      setTimeout(() => { if (!menu.matches?.(':hover')) g.boxLeave(); }, 90);
    };
    settle();
  });

  // Single-click cycles the accent to the next preset (a CUSTOM colour resets to the
  // default), deferred briefly so a double-click cancels it. The logo is handed over as
  // the swap origin — the palette floods out of the badge you clicked (desktop parity:
  // mainWindow.cpp anchors its accent cycle to the logo too).
  const cycleAccent = () => {
    if (app.customAccent) { app.setAccent(DEFAULT_ACCENT, logo); return; }
    const keys = ACCENTS.map((a) => a.key);
    const i = keys.indexOf(app.accent);
    app.setAccent(keys[(i + 1) % keys.length], logo);
  };
  let clickTimer = null;
  logo.addEventListener('click', (e) => {
    // Alt+click opens the menu as a PEEK instead of cycling (never schedules the
    // deferred cycle). Menu already open: an Alt-opened one treats the click as part
    // of the hold gesture (no-op — Alt's release governs); a sticky one toggles closed.
    if (e.altKey) {
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
      if (menuShowing()) { if (menuKind !== 'peek') closeMenu(); return; }
      altPeek();
      return;
    }
    if (clickTimer) return;   // second click of a dbl — let dblclick handle it
    clickTimer = setTimeout(() => { clickTimer = null; cycleAccent(); }, 220);
  });

  // A tiny, near-invisible colour input parked under the logo. It stays in normal flow
  // (not display:none / zero-size) so the browser will actually render its native picker.
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.setAttribute('aria-hidden', 'true');
  picker.tabIndex = -1;
  picker.style.cssText = 'position:absolute;width:1px;height:1px;opacity:0;border:0;padding:0;pointer-events:none;';
  logo.insertAdjacentElement('afterend', picker);

  // Same origin as the cycle: the native picker is an OS window, so there is no press in
  // the page to read while the user drags around it.
  const apply = () => app.setCustomAccent(picker.value, logo); // native colour input yields #rrggbb
  picker.addEventListener('input', apply);   // live while dragging
  picker.addEventListener('change', apply);  // final commit

  const open = () => {
    const cur = app.customAccent || getComputedStyle(document.documentElement).getPropertyValue('--accent');
    picker.value = normalizeHex(cur) || accentHex(app.accent);
    // showPicker() is the reliable way to open a picker programmatically (a bare .click()
    // on a hidden input often won't); fall back to click() on older browsers.
    try {
      if (typeof picker.showPicker === 'function') picker.showPicker();
      else picker.click();
    } catch {
      picker.click();
    }
  };
  logo.addEventListener('dblclick', () => {
    if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }   // cancel the single-click cycle
    open();
  });
  // A double-click selects nearby text; clear it so the picker isn't fighting a selection.
  logo.addEventListener('mousedown', (e) => { if (e.detail > 1) e.preventDefault(); });

  // dblclick is unreliable on touch — detect a double-tap by hand.
  let lastTap = 0;
  logo.addEventListener('touchend', (e) => {
    const now = Date.now();
    if (now - lastTap < 400) {
      e.preventDefault();
      lastTap = 0;
      open();
    } else {
      lastTap = now;
    }
  });
}

define('stencil-toolbar', StencilToolbar);
