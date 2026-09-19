// The toolbar's top bar: the logo (with its accent menu), the collapse toggle and the
// project-name field with its ? hints badge. ui/toolbar.js composes it into inner().
import { icon } from './icons.js';

export const toolbarTopbarHtml = () => `            <div class="controls-topbar">
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
            </div>`;
