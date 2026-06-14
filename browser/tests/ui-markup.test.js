import { test } from 'node:test';
import assert from 'node:assert';

// Layout transitively requires every ui component and wires them into globalThis.
import { layout } from '../js/ui/layout.js';

const markup = layout();

// Count occurrences of a needle within the markup.
const count = (needle) => markup.split(needle).length - 1;

// All 156 static body IDs (spec §6 + crop modal), original body order. Each must appear EXACTLY once.
const IDS = [
    'ctxMenu', 'ctx-layout-menu', 'ctx-layout-sub', 'ctx-copy-img', 'ctx-paste-img', 'ctx-dl-img',
    'ctx-copy-layout', 'ctx-paste-layout', 'ctx-dl-layout', 'ctx-ul-layout', 'ctx-fullscreen',
    'ctx-fs-label', 'ctx-fit-window', 'ctx-draw-toggle', 'ctx-draw-label', 'ctx-draw-hotkey',
    'ctx-drawmode-toggle', 'ctx-drawmode-label', 'ctx-draw-rect', 'ctx-show-points', 'ctx-chk-points',
    'ctx-show-lines', 'ctx-chk-lines', 'ctx-clear-lines', 'ctx-style-menu', 'ctx-style-sub',
    'ctx-marker-size', 'ctx-thickness', 'ctx-style-radios', 'ctx-filter-menu', 'ctx-filter-sub',
    'ctx-filter-radios', 'ctxTintRow', 'ctx-tint-color', 'ctx-transform-menu', 'ctx-transform-sub',
    'ctx-allow-formulas', 'ctx-formula-inputs', 'ctx-formula-x', 'ctx-formula-y', 'ctx-formula-error',
    'ctx-tooltip-menu', 'ctx-tooltip-sub', 'ctx-tt-enabled', 'ctx-tt-page', 'ctx-tt-screen', 'ctx-tt-coords',
    'fs-top-trigger', 'fs-right-trigger', 'fs-controls-panel', 'fs-exit-btn', 'fs-selection-panel',
    'fs-points-panel', 'globalDropOverlay', 'toggleControls', 'hintsBtn', 'hintsPopup', 'controlsBody',
    'imageUpload', 'imageSizeDisplay', 'imageFilter', 'filterColor', 'cropImage', 'lineColor', 'lineThickness',
    'markerSize', 'lineStyle', 'startDrawing', 'stopDrawing', 'drawModeToggle', 'undo', 'redo', 'showPoints',
    'showLines', 'clearAllLines', 'zoomOut', 'zoomInput', 'zoomIn', 'zoomFit', 'pageSize', 'unitSelect', 'customSizeGroup',
    'customPageWidth', 'customPageHeight', 'customUnitLabel', 'allowFormulas', 'formulaInputs', 'formulaX', 'formulaY',
    'formulaError', 'downloadJSON', 'copyJSONBtn', 'saveImage', 'uploadJSON', 'uploadJSONBtn', 'clearStorage',
    'saveStatus', 'themeToggle', 'fullscreenToggle', 'settingsBtn', 'visualsBtn', 'infoBtn', 'selectionPanel',
    'selColor', 'selThickness', 'selMarkerSize', 'selStyle', 'selFillGroup', 'selFillEnabled', 'selFill',
    'selFillClear', 'selDeselect', 'imageInfo', 'canvasViewport', 'canvasContainer', 'canvas', 'zoomRectOverlay',
    'tooltip', 'coordStatus', 'coordPanel', 'coordPanelHeader', 'coordTitle', 'toggleCoordPanel', 'coordBody', 'coordinatesTable',
    'coordinatesBody', 'notifyBalloon', 'settingsModalOverlay', 'settingsModal', 'settingsClose', 'hotkeyTable',
    'resetAllHotkeys', 'visualsModalOverlay', 'visualsClose', 'vs-line-color', 'vs-thickness', 'vs-marker',
    'vs-style', 'vs-fill', 'vs-sel-glow', 'vs-hover-ring', 'vs-focus-ring', 'vs-reset', 'infoModalOverlay',
    'infoClose', 'infoSearch', 'infoBody',
    // Crop modal (stencil-crop-modal) + its toolbar trigger (cropImage, above).
    'cropModalOverlay', 'cropClose', 'cropStage', 'cropImageEl', 'cropBox',
    'cropDims', 'cropOrientation', 'cropCancel', 'cropApply'
];

test('fixture has exactly 156 IDs', () => {
    assert.strictEqual(IDS.length, 156);
});

test('every static body ID is present exactly once', () => {
    for (const id of IDS) {
        assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
    }
});

test('dynamic containers are present and empty/placeholder', () => {
    // hotkey table tbody is empty in static markup
    assert.ok(markup.includes('<tbody></tbody>'), 'empty hotkey <tbody> present');
    // #infoBody is empty (comment-only) in static markup
    assert.ok(/<div class="settings-body" id="infoBody"><!-- filled by JS --><\/div>/.test(markup),
        'empty #infoBody present');
    // #coordinatesBody has only the placeholder row
    assert.ok(markup.includes('<tbody id="coordinatesBody">'), '#coordinatesBody present');
    assert.ok(markup.includes('<td colspan="6" class="empty-message">No points yet.</td>'),
        'coord placeholder row present');
});

test('context-menu data-hk attributes are present', () => {
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
    assert.ok(markup.includes('id="showPoints" checked'), 'showPoints checked');
    assert.ok(markup.includes('id="showLines" checked'), 'showLines checked');
});

test('disabled defaults preserved', () => {
    assert.ok(markup.includes('id="stopDrawing" disabled'), 'stopDrawing disabled');
    assert.ok(markup.includes('id="undo" disabled'), 'undo disabled');
    assert.ok(markup.includes('id="redo" disabled'), 'redo disabled');
});

test('value defaults preserved', () => {
    assert.ok(markup.includes('id="filterColor" value="#7c3aed"'), 'filterColor default');
    assert.ok(markup.includes('id="lineColor" value="#FFFF00"'), 'lineColor default');
    assert.ok(markup.includes('id="lineThickness" value="2"'), 'lineThickness default');
    assert.ok(markup.includes('id="markerSize" value="4"'), 'markerSize default');
    assert.ok(markup.includes('id="zoomInput" value="100"'), 'zoomInput default');
    assert.ok(markup.includes('id="customPageWidth" value="21"'), 'customPageWidth default');
    assert.ok(markup.includes('id="customPageHeight" value="29.7"'), 'customPageHeight default');
});

test('HTML entities preserved (not decoded)', () => {
    assert.ok(markup.includes('B&amp;W'), 'B&amp;W entity preserved');
    assert.ok(markup.includes('Black &amp; White'), 'Black &amp; White entity preserved');
    assert.ok(markup.includes('Controls &amp; Shortcuts'), 'Controls &amp; Shortcuts entity preserved');
    assert.ok(markup.includes('&nbsp;'), '&nbsp; entity preserved');
});

test('zoomInput appears exactly once in static markup', () => {
    assert.strictEqual(count('id="zoomInput"'), 1);
});

test('runtime-only ids are NOT in static markup', () => {
    assert.strictEqual(count('fs-clone-'), 0, 'no fs-clone-* in static markup');
    assert.strictEqual(count('fs-coordPanel-clone'), 0, 'no fs-coordPanel-clone in static markup');
    assert.strictEqual(count('imageMissingBanner'), 0, 'no imageMissingBanner in static markup');
});

test('no stray backtick or template interpolation leaked into markup', () => {
    assert.strictEqual(count('`'), 0, 'no backticks in markup');
    assert.strictEqual(count('${'), 0, 'no ${ in markup');
});
