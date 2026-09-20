import { test } from 'node:test';
import assert from 'node:assert';

// Layout transitively requires every ui component and wires them into globalThis.
import { layout } from '../js/ui/layout.js';

const markup = layout();

const count = (needle) => markup.split(needle).length - 1;

// Every static body ID (spec §6 plus the crop / install / open-image / open-in / confirm
// modals and the AI chat panel), in body order; each must appear EXACTLY once.
const IDS = [
    'ctx-menu', 'ctx-layout-menu', 'ctx-layout-sub', 'ctx-copy-img', 'ctx-copy-img-sub',
    'ctx-copy-img-split', 'ctx-copy-img-split-hk', 'ctx-copy-img-current', 'ctx-copy-img-current-hk',
    'ctx-copy-img-original', 'ctx-copy-img-tint', 'ctx-paste-img',
    'ctx-dl-img', 'ctx-dl-img-sub', 'ctx-dl-img-split', 'ctx-dl-img-split-hk', 'ctx-dl-img-current',
    'ctx-dl-img-current-hk', 'ctx-dl-img-original', 'ctx-dl-img-tint',
    'ctx-copy-layout', 'ctx-paste-layout', 'ctx-dl-layout', 'ctx-ul-layout', 'ctx-fullscreen',
    'ctx-fs-label', 'ctx-fit-window', 'ctx-draw-toggle', 'ctx-draw-label', 'ctx-draw-hotkey',
    'ctx-draw-line', 'ctx-draw-rect', 'ctx-show-points', 'ctx-chk-points',
    'ctx-show-lines', 'ctx-chk-lines', 'ctx-clear-lines', 'ctx-style-menu', 'ctx-style-sub',
    'ctx-point-size', 'ctx-thickness', 'ctx-style-radios', 'ctx-filter-menu', 'ctx-filter-sub',
    'ctx-filter-radios', 'ctx-tint-row', 'ctx-tint-color', 'ctx-transform-menu', 'ctx-transform-sub',
    'ctx-allow-formulas', 'ctx-formula-inputs', 'ctx-formula-x', 'ctx-formula-y', 'ctx-formula-error',
    'ctx-tooltip-menu', 'ctx-tooltip-sub', 'ctx-tt-enabled', 'ctx-tt-page', 'ctx-tt-screen', 'ctx-tt-coords',
    'fs-top-trigger', 'fs-right-trigger', 'fs-controls-panel', 'fs-selection-panel',
    'fs-points-panel', 'global-drop-overlay', 'toggle-controls', 'hints-btn', 'hints-popup', 'controls-body',
    'image-filter', 'filter-color', 'crop-image', 'line-color', 'line-thickness',
    'point-size', 'line-style', 'draw-toggle', 'draw-mode-toggle', 'undo', 'redo', 'show-points',
    'show-lines', 'clear-all-lines', 'zoom-out', 'zoom-input', 'zoom-menu', 'zoom-in', 'zoom-fit', 'page-size', 'unit-select', 'custom-size-group',
    'custom-page-width', 'custom-page-height', 'allow-formulas', 'formula-inputs', 'formula-x', 'formula-y',
    'formula-error', 'download-json', 'copy-json-btn', 'save-image', 'upload-json', 'upload-json-btn', 'clear-storage',
    'theme-toggle', 'fullscreen-toggle', 'settings-btn', 'visuals-btn', 'info-btn', 'selection-panel',
    'sel-color', 'sel-thickness', 'sel-point-size', 'sel-style', 'sel-fill-group', 'sel-fill',
    'sel-fill-clear', 'sel-deselect', 'image-info', 'canvas-viewport', 'canvas-container', 'canvas', 'zoom-rect-overlay',
    'tooltip', 'coord-status', 'coord-panel', 'coord-panel-header', 'coord-tab-points', 'coord-tab-lines', 'toggle-coord-panel', 'coord-body', 'coordinates-table',
    'coordinates-body', 'lines-list', 'notify-balloon', 'settings-modal-overlay', 'settings-modal', 'settings-close', 'hotkey-table',
    'reset-all-hotkeys', 'visuals-modal-overlay', 'visuals-close', 'vs-line-color', 'vs-thickness', 'vs-point',
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
    // Description & attributes section (project meta): description + keywords buttons; links-btn moved here.
    'description-btn', 'keywords-btn',
    // Context-menu Share Image item.
    'ctx-share-img',
    // Unified Open Image modal (stencil-open-image-modal): Local file / URL link / Blank tabs.
    'open-image-modal-overlay', 'open-image-close', 'open-image-file', 'open-image-incognito',
    'open-image-cancel', 'open-image-here', 'open-image-newtab', 'open-image-incognito-row',
    'oi-tab-file', 'oi-tab-url', 'oi-tab-blank', 'oi-panel-file', 'oi-panel-url', 'oi-panel-blank',
    // #open-in-telegram is always in the markup but hidden until the async openInConfig.json
    // provides a bot username.
    'open-in-btn', 'open-in-modal-overlay', 'open-in-close', 'open-in-status-row', 'open-in-status',
    'open-in-incognito', 'open-in-fallback-row', 'open-in-fallback-cmds', 'open-in-fallback-copy',
    'open-in-hint', 'open-in-cancel', 'open-in-desktop', 'open-in-telegram',
    // Generic confirm modal (stencil-confirm-modal) replacing native confirm().
    'confirm-modal-overlay', 'confirm-modal-close', 'confirm-modal-title', 'confirm-modal-title-text',
    'confirm-modal-message', 'confirm-modal-cancel', 'confirm-modal-cancel-text',
    'confirm-modal-confirm', 'confirm-modal-confirm-text',
    // AI assistant: toolbar toggle + chat panel (stencil-chat-panel) + settings modal
    // (stencil-llm-settings-modal), appended at the END of REGIONS.
    'chat-btn', 'voice-chat-btn', 'chat-panel', 'chat-header', 'chat-title', 'chat-status-dot', 'chat-dock-left-btn',
    'chat-dock-top-btn', 'chat-dock-bottom-btn', 'chat-dock-right-btn', 'chat-float-btn',
    'chat-settings-btn', 'chat-close', 'chat-transcript', 'chat-empty', 'chat-attachments',
    'chat-attach-btn', 'chat-attach-input', 'chat-input', 'chat-send', 'chat-resizer',
    'chat-settings-overlay', 'chat-settings-close', 'chat-provider', 'chat-base-url-row',
    'chat-base-url', 'chat-model', 'chat-api-key-row', 'chat-api-key', 'chat-server-row',
    'chat-server-select', 'chat-server-status-row', 'chat-server-status', 'chat-cors-note',
    // Project description / keywords modals (stencil-description-modal, stencil-keywords-modal).
    'description-overlay', 'description-close', 'description-text', 'description-cancel', 'description-save',
    'keywords-overlay', 'keywords-close', 'keywords-input', 'keywords-add', 'keywords-chips', 'keywords-clear', 'keywords-cancel', 'keywords-save'
];

test('fixture has exactly 261 IDs', () => {
    assert.strictEqual(IDS.length, 261);   // +3: keywords-text became input + add + chips + clear
});

test('every static body ID is present exactly once', () => {
    for (const id of IDS) {
        assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
    }
});

test('dynamic containers are present and empty/placeholder', () => {
    assert.ok(markup.includes('<div class="hotkey-rows" role="rowgroup"></div>'), 'empty hotkey rows container present');
    assert.ok(/<div class="settings-body" id="info-body"><!-- filled by JS --><\/div>/.test(markup),
        'empty #info-body present');
    assert.ok(markup.includes('<tbody id="coordinates-body">'), '#coordinates-body present');
    assert.ok(markup.includes('<td colspan="6" class="empty-message">No points yet.</td>'),
        'coord placeholder row present');
});

test('context-menu data-hk attributes are present', () => {
    // Every data-hk must match a real hotkey registry id so formatCombo can render it
    // ('clearAllLines', not 'clear-all-lines'); a submenu opener carries none, its rows do.
    const hks = ['copyLayout', 'fullscreen', 'resetZoom', 'startDraw',
                 'togglePoints', 'toggleLines', 'clearAllLines', 'cycleFilter'];
    for (const hk of hks) {
        assert.ok(markup.includes(`data-hk="${hk}"`), `data-hk="${hk}" present`);
    }
    // The menu's own two paste rows; the drop hint carries a third data-hk elsewhere.
    assert.strictEqual(count('class="ctx-hotkey" data-hk="paste"'), 2, 'two paste rows');
});

test('Copy Image / Download Image opener rows carry no hotkey chip of their own', () => {
    assert.ok(!markup.includes('data-hk="copyImage"'), 'ctx-copy-img no longer hints Ctrl+C');
    assert.ok(!markup.includes('data-hk="saveImage"'), 'ctx-dl-img no longer hints Ctrl+Shift+D');
});

test('checked defaults preserved', () => {
    for (const id of ['ctx-tt-enabled', 'ctx-tt-page', 'ctx-tt-screen', 'ctx-tt-coords']) {
        assert.ok(markup.includes(`id="${id}" checked`), `${id} is checked`);
    }
    assert.ok(markup.includes('id="show-points" checked'), 'showPoints checked');
    assert.ok(markup.includes('id="show-lines" checked'), 'showLines checked');
});
