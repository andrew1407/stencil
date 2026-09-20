import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { wireHoverDust, foldDust } from './motion.js';
import { onWindowResize } from '../utils.js';
import { subscribe, EVENTS } from '../eventBus/appBus.js';
import { syncWrappedSeparators } from './toolbarSeparators.js';
import { wireVoiceChatToggle } from './voiceToggle.js';
import { wireLogoColorPicker } from './logoAccent.js';
import { wireLogoHold } from './logoStageTrigger.js';
import { toolbarTopbarHtml } from './toolbarTopbar.js';
import { toolbarImageSectionsHtml, toolbarStyleSectionsHtml } from './toolbarSections.js';
import { toolbarPageSectionsHtml } from './toolbarPageSections.js';
// Owns the controls markup and the collapse/hints behaviour; the individual inputs and buttons
// are wired by DrawingApp via global ids.
export class StencilToolbar extends StencilElement {
  static inner() {
    return `
${toolbarTopbarHtml()}
            <div id="controls-body">
        <div class="controls">

${toolbarImageSectionsHtml()}

${toolbarStyleSectionsHtml()}

${toolbarPageSectionsHtml()}

            <!-- ── Section: Data ── -->
            <div class="ctrl-section">
                <div class="ctrl-section-label">Data</div>
                <div class="ctrl-section-row">
                    <button id="script-btn" class="btn-icon" data-hk-title="openScript" data-title="Stencil script (.stc) — write and run a script over this project">${icon('script')}</button>
                    <button id="copy-json-btn" class="btn-icon" data-hk-title="copyLayout" data-title="Copy full Layout JSON (lines + all applied edits)" data-disabled-reason="Draw at least one line to copy">${icon('clipboard')}</button>
                    <button id="download-json" class="btn-icon" data-hk-title="downloadJson" data-title="Download Layout JSON" data-disabled-reason="Draw at least one line to export">${icon('file-down')}</button>
                    <input type="file" id="upload-json" accept=".json" style="display:none;">
                    <button id="upload-json-btn" class="btn-icon" data-hk-title="uploadJson" data-title="Upload Layout JSON" data-disabled-reason="Load an image first">${icon('file-up')}</button>
                    <button id="clear-storage" class="danger btn-icon" data-hk-title="clearProject" data-title="Remove current project" data-disabled-reason="Open an image first — nothing to remove">${icon('trash')}</button>
                </div>
            </div>

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

    const refresh = () => {
      const el = document.getElementById('image-info');
      // data-size is the info line's OWN text — its incognito tag is a child element,
      // and this bubble states that fact on its own line below.
      const size = el ? (el.dataset.size ?? el.textContent) : '';
      const incognito = document.body.classList.contains('incognito-mode');
      const hasImage = /^Image Size:/.test(size);
      // Only while the toolbar is COLLAPSED: with the tool rows up the info line already says this,
      // and folded away it is hidden too (layout/infoLine.css).
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
    wireLogoHold(this.querySelector('.app-logo'), _app);
    wireVoiceChatToggle(this.querySelector('#voice-chat-btn'), _app);
    // The section separators follow the wrap (below): measured again whenever this
    // toolbar, or the window around the fullscreen clone, changes size.
    const syncSeps = () => {
      syncWrappedSeparators(this);
      const fs = document.getElementById('fs-controls-panel');
      if (fs) syncWrappedSeparators(fs);
    };
    // Every SECTION is watched too, not just the toolbar: the f(x,y) fields and the custom page's
    // W/H boxes re-wrap their row while the toolbar's own box never moves.
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
