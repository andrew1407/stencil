import { StencilElement, hostTag, define } from './base.js';
import { StencilTooltip } from './tooltip.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { wirePanelResizer } from '../utils.js';
import { foldDust, motionReduced } from './motion.js';
// ── Component: main content (canvas section + coordinates panel) ──
// Owns the canvas/coord-panel markup and the coord-panel collapse behavior.
export class StencilMainContent extends StencilElement {
  static inner() {
    return `
            <div class="canvas-section">
                <div class="canvas-viewport" id="canvas-viewport">
                    <!-- Incognito frame. Four separate edges rather than one CSS outline,
                         because the dashes have to DRAW ON clockwise from the top-left and
                         retract the same way out — an outline can only appear all at once.
                         Always in the DOM at zero length; body.incognito-mode gives the
                         edges their length. It traces the VIEWPORT — the whole visible
                         canvas region, picture and the empty ground beside it alike — so it
                         reads as "this editor is incognito", not as a box around the image.
                         Sticky, so it holds that edge at any zoom AND scroll offset, and
                         its height is cancelled again by the negative margin: the frame
                         must not push the canvas down by a viewport's worth of space. -->
                    <div class="incognito-frame" aria-hidden="true">
                        <i class="ig-edge ig-t"></i><i class="ig-edge ig-r"></i>
                        <i class="ig-edge ig-b"></i><i class="ig-edge ig-l"></i>
                    </div>
                    <div class="canvas-container" id="canvas-container">
                        <!-- tabindex makes the canvas focusable so a click can pull focus off the
                             chat textarea (controlsBinder pointerdown); -1 keeps it off Tab. -->
                        <canvas id="canvas" tabindex="-1"></canvas>
                        <div id="zoom-rect-overlay" style="display:none;position:absolute;border:2px dashed #7c3aed;background:rgba(124,58,237,0.08);pointer-events:none;box-sizing:border-box;"></div>
                        ${StencilTooltip.template()}
                    </div>
                    <div class="idle-create" id="idle-create-wrap">
                        <button id="create-blank-btn" class="idle-create-btn">
                            <span class="idle-create-icon">${icon('image', { size: 32 })}</span>
                            <span>＋ Blank image</span>
                        </button>
                    </div>
                </div>
                <div class="coord-status" id="coord-status"></div>
                <div class="drop-hint">${icon('lightbulb', { size: 14 })} Drag &amp; drop an <strong>image</strong> or <strong>.json</strong> anywhere on the page — or paste an image with <strong>Ctrl+V</strong></div>
            </div>

            <!-- Drag handle to resize the coordinates panel (browser parity with the desktop
                 canvas↔panel splitter). Hidden while the panel is collapsed. -->
            <div class="panel-resizer" id="panel-resizer"></div>

            <div class="coordinates-panel" id="coord-panel">
                <div class="coord-panel-header" id="coord-panel-header">
                    <div class="coord-tabs" role="tablist">
                        <button id="coord-tab-points" class="coord-tab coord-tab-active" role="tab" aria-selected="true" data-tab="points" data-title="Points of the selected line">Points</button>
                        <button id="coord-tab-lines" class="coord-tab" role="tab" aria-selected="false" data-tab="lines" data-title="All lines — select, inspect or remove">Lines</button>
                    </div>
                    <button id="toggle-coord-panel" class="btn-icon" data-hk-title="togglePointsList" data-title="Hide panel">${icon('chevron-right')}</button>
                </div>
                <div id="coord-body">
                <table class="coordinates-table" id="coordinates-table">
                    <thead>
                        <tr>
                            <th>#</th>
                            <th>X px</th>
                            <th>Y px</th>
                            <th>X cm</th>
                            <th>Y cm</th>
                            <th style="width:28px;padding:4px;"></th>
                        </tr>
                    </thead>
                    <tbody id="coordinates-body">
                        <tr>
                            <td colspan="6" class="empty-message">No points yet.</td>
                        </tr>
                    </tbody>
                </table>
                <div id="lines-list" class="lines-list" role="tabpanel" style="display:none;"></div>
                </div>
            </div>
    `;
  }
  static template() { return hostTag('stencil-main-content', 'class="main-content"', StencilMainContent.inner()); }

  wire(app) {
    // The incognito frame is sticky INSIDE the scrolling viewport, so it needs the
    // viewport's visible size in px — a percentage resolves against the scrollable
    // content, exactly the box the frame must NOT trace. Resize only: sticky handles
    // scrolling itself, so nothing runs per scroll frame.
    const vp = document.getElementById('canvas-viewport');
    if (vp) {
      const syncFrameBox = () => {
        vp.style.setProperty('--vp-w', `${vp.clientWidth}px`);
        vp.style.setProperty('--vp-h', `${vp.clientHeight}px`);
      };
      if (typeof ResizeObserver !== 'undefined') new ResizeObserver(syncFrameBox).observe(vp);
      syncFrameBox();
    }
    const btn = document.getElementById('toggle-coord-panel');
    const panel = document.getElementById('coord-panel');
    const body = document.getElementById('coord-body');
    let hidden = false;
    let foldTimer = 0;

    btn.addEventListener('click', () => {
      hidden = !hidden;
      // The shared fold-with-dust ritual (motion.js foldDust): the table pours out past
      // the right edge the panel collapses towards and gathers back out of it. Its own
      // gather clock (460): .coord-folding below takes the table out of the layout for
      // half the slide, which restarts the surfaceForm fade — a window's 620ms gather
      // would still be fading long after the panel had settled.
      clearTimeout(foldTimer);
      panel.classList.remove('coord-folding');
      foldDust(body, panel, 'coord-collapsed', hidden, 'right',
        { inMs: 460, toggle: () => panel.classList.toggle('coord-collapsed', hidden) });
      // Hold the tabs/table out of the layout while the panel slides (.coord-folding,
      // animations.css). Half the slide's OWN duration — the collapse takes the longer
      // one — by which point the out-quart ease is ~94% done.
      const reduced = motionReduced();
      const token = hidden ? '--fold-out-ms' : '--fold-ms';
      const foldMs = parseFloat(getComputedStyle(document.documentElement).getPropertyValue(token))
                     || (hidden ? 600 : 400);
      panel.classList.add('coord-folding');
      foldTimer = setTimeout(() => panel.classList.remove('coord-folding'), reduced ? 0 : foldMs / 2);
      // The panel collapses to a right-hand rail, so the chevron points RIGHT to hide and
      // LEFT to show. Not swapped: animations.css spins the one glyph 180° with the slide.
      btn.dataset.title = hidden ? 'Show Last Line Points' : 'Hide panel';
      btn.dataset.tip = hotkeys.hkTitle(hidden ? 'Show Last Line Points' : 'Hide panel', 'togglePointsList');
    });

    // Points | Lines tabs — the Points tab is the existing per-line coordinate table;
    // the Lines tab lists every committed line (select/inspect/remove). Switching to Lines
    // reveals #lines-list and asks the app to (re)build it; back to Points restores the table.
    const tabPoints = document.getElementById('coord-tab-points');
    const tabLines = document.getElementById('coord-tab-lines');
    const table = document.getElementById('coordinates-table');
    const linesList = document.getElementById('lines-list');
    const selectTab = (which) => {
      const onLines = which === 'lines';
      tabLines.classList.toggle('coord-tab-active', onLines);
      tabPoints.classList.toggle('coord-tab-active', !onLines);
      tabLines.setAttribute('aria-selected', onLines ? 'true' : 'false');
      tabPoints.setAttribute('aria-selected', onLines ? 'false' : 'true');
      table.style.display = onLines ? 'none' : '';
      linesList.style.display = onLines ? '' : 'none';
      if (onLines && app) app.renderLinesList();
    };
    tabPoints.addEventListener('click', () => selectTab('points'));
    tabLines.addEventListener('click', () => selectTab('lines'));

    // ── Resizable coordinates panel: drag #panel-resizer to set the panel width (persisted).
    // Mirrors the desktop canvas↔panel splitter. The panel sits on the RIGHT, so dragging the
    // handle LEFT widens it. Width lives in the --coord-panel-width CSS var on :root.
    const resizer = document.getElementById('panel-resizer');
    if (resizer) wirePanelResizer(resizer, panel, { maxFactor: 0.7, restore: true });
  }
}
define('stencil-main-content', StencilMainContent);
