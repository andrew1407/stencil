import { test } from 'node:test';
import assert from 'node:assert';

// Layout transitively requires every ui component and wires them into globalThis.
import { layout } from '../js/ui/layout.js';

const markup = layout();

const count = (needle) => markup.split(needle).length - 1;

// All 202 static body IDs (spec §6 + crop / install / open-image / open-in / confirm modals), original body order. Each must appear EXACTLY once.
const IDS = [
    'ctx-menu', 'ctx-layout-menu', 'ctx-layout-sub', 'ctx-copy-img', 'ctx-paste-img', 'ctx-dl-img',
    'ctx-copy-layout', 'ctx-paste-layout', 'ctx-dl-layout', 'ctx-ul-layout', 'ctx-fullscreen',
    'ctx-fs-label', 'ctx-fit-window', 'ctx-draw-toggle', 'ctx-draw-label', 'ctx-draw-hotkey',
    'ctx-drawmode-toggle', 'ctx-drawmode-label', 'ctx-draw-rect', 'ctx-show-points', 'ctx-chk-points',
    'ctx-show-lines', 'ctx-chk-lines', 'ctx-clear-lines', 'ctx-style-menu', 'ctx-style-sub',
    'ctx-marker-size', 'ctx-thickness', 'ctx-style-radios', 'ctx-filter-menu', 'ctx-filter-sub',
    'ctx-filter-radios', 'ctx-tint-row', 'ctx-tint-color', 'ctx-transform-menu', 'ctx-transform-sub',
    'ctx-allow-formulas', 'ctx-formula-inputs', 'ctx-formula-x', 'ctx-formula-y', 'ctx-formula-error',
    'ctx-tooltip-menu', 'ctx-tooltip-sub', 'ctx-tt-enabled', 'ctx-tt-page', 'ctx-tt-screen', 'ctx-tt-coords',
    'fs-top-trigger', 'fs-right-trigger', 'fs-controls-panel', 'fs-exit-btn', 'fs-selection-panel',
    'fs-points-panel', 'global-drop-overlay', 'toggle-controls', 'hints-btn', 'hints-popup', 'controls-body',
    'image-filter', 'filter-color', 'crop-image', 'line-color', 'line-thickness',
    'marker-size', 'line-style', 'start-drawing', 'stop-drawing', 'draw-mode-toggle', 'undo', 'redo', 'show-points',
    'show-lines', 'clear-all-lines', 'zoom-out', 'zoom-input', 'zoom-menu', 'zoom-in', 'zoom-fit', 'page-size', 'unit-select', 'custom-size-group',
    'custom-page-width', 'custom-page-height', 'custom-unit-label', 'allow-formulas', 'formula-inputs', 'formula-x', 'formula-y',
    'formula-error', 'download-json', 'copy-json-btn', 'save-image', 'upload-json', 'upload-json-btn', 'clear-storage',
    'save-status', 'theme-toggle', 'fullscreen-toggle', 'settings-btn', 'visuals-btn', 'info-btn', 'selection-panel',
    'sel-color', 'sel-thickness', 'sel-marker-size', 'sel-style', 'sel-fill-group', 'sel-fill-enabled', 'sel-fill',
    'sel-fill-clear', 'sel-deselect', 'image-info', 'canvas-viewport', 'canvas-container', 'canvas', 'zoom-rect-overlay',
    'tooltip', 'coord-status', 'coord-panel', 'coord-panel-header', 'coord-tab-points', 'coord-tab-lines', 'toggle-coord-panel', 'coord-body', 'coordinates-table',
    'coordinates-body', 'lines-list', 'notify-balloon', 'settings-modal-overlay', 'settings-modal', 'settings-close', 'hotkey-table',
    'reset-all-hotkeys', 'visuals-modal-overlay', 'visuals-close', 'vs-line-color', 'vs-thickness', 'vs-marker',
    'vs-style', 'vs-fill', 'vs-sel-glow', 'vs-hover-ring', 'vs-focus-ring', 'vs-reset', 'info-modal-overlay',
    'info-close', 'info-search', 'info-body',
    // Crop modal (stencil-crop-modal) + its toolbar trigger (cropImage, above).
    'crop-modal-overlay', 'crop-close', 'crop-stage', 'crop-image-el', 'crop-box',
    'crop-dims', 'crop-orientation', 'crop-cancel', 'crop-apply',
    // Install / download button (stencil-install): small bottom-right icon with a
    // hover menu — PWA option appears only after `beforeinstallprompt`.
    'install-host', 'install-menu', 'install-pwa-btn', 'install-desktop-btn', 'install-toggle',
    // State-aware Image section: compact load button + image-actions group (download/copy/share/open).
    'load-image-btn', 'image-actions', 'copy-image', 'share-image', 'open-image-btn',
    // Context-menu Share Image item.
    'ctx-share-img',
    // Unified Open Image modal (stencil-open-image-modal): Local file / URL link / Blank tabs.
    'open-image-modal-overlay', 'open-image-close', 'open-image-file', 'open-image-incognito',
    'open-image-cancel', 'open-image-here', 'open-image-newtab', 'open-image-incognito-row',
    'oi-tab-file', 'oi-tab-url', 'oi-tab-blank', 'oi-panel-file', 'oi-panel-url', 'oi-panel-blank',
    // Open-in-another-app modal (stencil-open-in-modal) + its toolbar trigger. The
    // #open-in-telegram button is always in the markup but hidden until the local
    // openInConfig.json (loaded async) provides a bot username.
    'open-in-btn', 'open-in-modal-overlay', 'open-in-close', 'open-in-status-row', 'open-in-status',
    'open-in-incognito', 'open-in-fallback-row', 'open-in-fallback-cmds', 'open-in-fallback-copy',
    'open-in-hint', 'open-in-cancel', 'open-in-desktop', 'open-in-telegram',
    // Generic confirm modal (stencil-confirm-modal) replacing native confirm().
    'confirm-modal-overlay', 'confirm-modal-close', 'confirm-modal-title', 'confirm-modal-title-text',
    'confirm-modal-message', 'confirm-modal-cancel', 'confirm-modal-cancel-text',
    'confirm-modal-confirm', 'confirm-modal-confirm-text'
];

test('fixture has exactly 204 IDs', () => {
    assert.strictEqual(IDS.length, 204);
});

test('every static body ID is present exactly once', () => {
    for (const id of IDS) {
        assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
    }
});

test('dynamic containers are present and empty/placeholder', () => {
    assert.ok(markup.includes('<tbody></tbody>'), 'empty hotkey <tbody> present');
    assert.ok(/<div class="settings-body" id="info-body"><!-- filled by JS --><\/div>/.test(markup),
        'empty #info-body present');
    assert.ok(markup.includes('<tbody id="coordinates-body">'), '#coordinates-body present');
    assert.ok(markup.includes('<td colspan="6" class="empty-message">No points yet.</td>'),
        'coord placeholder row present');
});

test('context-menu data-hk attributes are present', () => {
    // Every data-hk must match a real hotkey registry id so formatCombo can render
    // it (mac glyphs incl.) — 'clearAllLines', not the legacy 'clear-all-lines'.
    const hks = ['copyImage', 'copyLayout', 'fullscreen', 'resetZoom', 'startDraw',
                 'togglePoints', 'toggleLines', 'clearAllLines', 'cycleFilter'];
    for (const hk of hks) {
        assert.ok(markup.includes(`data-hk="${hk}"`), `data-hk="${hk}" present`);
    }
    // 'paste' appears twice (paste image + paste layout)
    assert.strictEqual(count('data-hk="paste"'), 2, 'data-hk="paste" appears twice');
});

test('checked defaults preserved', () => {
    for (const id of ['ctx-tt-enabled', 'ctx-tt-page', 'ctx-tt-screen', 'ctx-tt-coords']) {
        assert.ok(markup.includes(`id="${id}" checked`), `${id} is checked`);
    }
    assert.ok(markup.includes('id="show-points" checked'), 'showPoints checked');
    assert.ok(markup.includes('id="show-lines" checked'), 'showLines checked');
});

test('disabled defaults preserved', () => {
    assert.ok(markup.includes('id="stop-drawing" disabled'), 'stopDrawing disabled');
    assert.ok(markup.includes('id="undo" disabled'), 'undo disabled');
    assert.ok(markup.includes('id="redo" disabled'), 'redo disabled');
});

test('value defaults preserved', () => {
    assert.ok(markup.includes('id="filter-color" value="#7c3aed"'), 'filterColor default');
    assert.ok(markup.includes('id="line-color" value="#FFFF00"'), 'lineColor default');
    assert.ok(markup.includes('id="line-thickness" value="2"'), 'lineThickness default');
    assert.ok(markup.includes('id="marker-size" value="4"'), 'markerSize default');
    assert.ok(markup.includes('id="zoom-input" value="100"'), 'zoomInput default');
    assert.ok(markup.includes('id="custom-page-width" value="21"'), 'customPageWidth default');
    assert.ok(markup.includes('id="custom-page-height" value="29.7"'), 'customPageHeight default');
});

test('filter selects offer every mode (toolbar options + ctx-menu radios)', () => {
    // Scope option checks to the #image-filter select itself — the page-size select
    // also has an <option value="custom">, which would otherwise mask a missing Tint.
    const filterSel = markup.slice(markup.indexOf('id="image-filter"'));
    const filterOptions = filterSel.slice(0, filterSel.indexOf('</select>'));
    for (const v of ['none', 'bw', 'sepia', 'invert', 'contour', 'custom']) {
        assert.ok(filterOptions.includes(`<option value="${v}"`), `filter option ${v} present`);
        assert.ok(markup.includes(`name="ctxFilter" value="${v}"`), `ctxFilter radio ${v} present`);
    }
    assert.ok(filterOptions.includes('<option value="custom">Tint</option>'), 'Tint option present');
    assert.ok(markup.includes('value="invert"> Invert'), 'Invert radio label');
    assert.ok(markup.includes('value="contour"> Contour'), 'Contour radio label');
});

test('page-size select: Custom… first, then the ISO formats labelled with sizes', () => {
    const sel = markup.slice(markup.indexOf('id="page-size"'));
    const custom = sel.indexOf('<option value="custom">Custom…</option>');
    assert.ok(custom !== -1, 'Custom… option present');
    assert.ok(custom < sel.indexOf('<option value="A0">'), 'Custom… before the named formats');
    // Spot-check the "<name> (<w> × <h> cm)" labels across the three series
    // (trailing zeros trimmed; values from PAGE_SIZES in constants.json).
    assert.ok(markup.includes('<option value="A4">A4 (21 × 29.7 cm)</option>'), 'A4 label');
    assert.ok(markup.includes('<option value="B5">B5 (17.6 × 25 cm)</option>'), 'B5 label');
    assert.ok(markup.includes('<option value="C10">C10 (2.8 × 4 cm)</option>'), 'C10 label');
});

test('HTML entities preserved (not decoded)', () => {
    assert.ok(markup.includes('B&amp;W'), 'B&amp;W entity preserved');
    assert.ok(markup.includes('Black &amp; White'), 'Black &amp; White entity preserved');
    assert.ok(markup.includes('Controls &amp; Shortcuts'), 'Controls &amp; Shortcuts entity preserved');
});

test('zoomInput appears exactly once in static markup', () => {
    assert.strictEqual(count('id="zoom-input"'), 1);
});

test('runtime-only ids are NOT in static markup', () => {
    assert.strictEqual(count('fs-clone-'), 0, 'no fs-clone-* in static markup');
    assert.strictEqual(count('fs-coord-panel-clone'), 0, 'no fs-coordPanel-clone in static markup');
    assert.strictEqual(count('image-missing-banner'), 0, 'no imageMissingBanner in static markup');
});

test('no stray backtick or template interpolation leaked into markup', () => {
    assert.strictEqual(count('`'), 0, 'no backticks in markup');
    assert.strictEqual(count('${'), 0, 'no ${ in markup');
});
