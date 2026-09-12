// ── Markup: the fullscreen trigger zones and the slide-in panels ───────────
// Empty shells: the controls strip and the points list are CLONED into them at runtime
// (ui/fullscreenClones.js) from the live panels, so there is one source of truth.
export const fullscreenLayerInner = () => `
    <!-- Fullscreen hover trigger zones -->
    <div id="fs-top-trigger"></div>
    <div id="fs-right-trigger"></div>

    <!-- Fullscreen slide-in: controls (top) -->
    <!-- No Exit button of its own: the revealed strip IS the toolbar, and the fullscreen
         toggle inside it (lit while on) is what leaves — plus Escape. A second control for
         the same thing sat over the cloned rows and read as part of them (user decision). -->
    <div id="fs-controls-panel">
        <!-- Controls content will be cloned here by JS -->
    </div>

    <!-- Fullscreen selection panel overlay (shown over canvas when a line is selected) -->
    <div id="fs-selection-panel" style="display:none;position:fixed;z-index:10001;left:0;right:0;pointer-events:auto;"></div>

    <!-- Fullscreen slide-in: points list (right) -->
    <div id="fs-points-panel">
        <!-- Coord panel content will be mirrored here by JS -->
    </div>
    <!-- Drag handle to resize the fullscreen points panel (sibling of the panel so the panel's
         innerHTML re-clone doesn't wipe it). Positioned at the panel's left edge via the width var. -->
    <div id="fs-panel-resizer"></div>
    `;
