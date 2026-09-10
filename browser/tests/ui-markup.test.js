import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// Layout transitively requires every ui component and wires them into globalThis.
import { layout } from '../js/ui/layout.js';

const markup = layout();

const count = (needle) => markup.split(needle).length - 1;

// All 247 static body IDs (spec §6 + crop / install / open-image / open-in / confirm modals + AI chat panel/settings), original body order. Each must appear EXACTLY once.
// ctx-*-img-split ("With Compare") is a fourth row, hidden until a split compare view is
// active, alongside ctx-copy-img-current / ctx-dl-img-current; each pair's own -hk span
// is where the shared hotkey chip moves to/from (contextMenu.js).
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
    // Open-in-another-app modal (stencil-open-in-modal) + its toolbar trigger. The
    // #open-in-telegram button is always in the markup but hidden until the local
    // openInConfig.json (loaded async) provides a bot username.
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
    'keywords-overlay', 'keywords-close', 'keywords-text', 'keywords-cancel', 'keywords-save'
];

test('fixture has exactly 258 IDs', () => {
    assert.strictEqual(IDS.length, 258);   // -1: the custom page boxes' unit suffix is gone
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
    // Every data-hk must match a real hotkey registry id so formatCombo can render
    // it (mac glyphs incl.) — 'clearAllLines', not the legacy 'clear-all-lines'.
    // 'copyImage'/'saveImage' are NOT here: the Copy/Download Image rows are nested
    // submenu openers now, and no longer carry the combo on the opener itself (it
    // lives on whichever variant row is primary — see ctx-copy-img's own comment).
    const hks = ['copyLayout', 'fullscreen', 'resetZoom', 'startDraw',
                 'togglePoints', 'toggleLines', 'clearAllLines', 'cycleFilter'];
    for (const hk of hks) {
        assert.ok(markup.includes(`data-hk="${hk}"`), `data-hk="${hk}" present`);
    }
    // 'paste' appears twice (paste image + paste layout)
    assert.strictEqual(count('data-hk="paste"'), 2, 'data-hk="paste" appears twice');
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

// The Draw group is two toggles that relabel themselves in place (Start↔Stop, Line↔Rect).
// Both must ship the width-pinning class, or the row reflows as they switch — and Start/Stop
// must be ONE control, not the old pair.
test('draw toggles are single, width-pinned controls', () => {
    assert.ok(!markup.includes('id="start-drawing"'), 'Start/Stop merged into one button');
    assert.ok(!markup.includes('id="stop-drawing"'), 'Start/Stop merged into one button');
    for (const id of ['draw-toggle', 'draw-mode-toggle']) {
        const tag = markup.match(new RegExp(`<button id="${id}"[^>]*>`));
        assert.ok(tag, `${id} present`);
        assert.ok(tag[0].includes('btn-draw-fixed'), `${id} must be width-pinned`);
    }
});

test('disabled defaults preserved', () => {
    assert.ok(markup.includes('id="undo" disabled'), 'undo disabled');
    assert.ok(markup.includes('id="redo" disabled'), 'redo disabled');
    assert.ok(markup.includes('id="chat-send" disabled'), 'chat send disabled until text is typed');
});

test('value defaults preserved', () => {
    assert.ok(markup.includes('id="filter-color" value="#7c3aed"'), 'filterColor default');
    assert.ok(markup.includes('id="line-color" value="#FFFF00"'), 'lineColor default');
    assert.ok(markup.includes('id="line-thickness" value="2"'), 'lineThickness default');
    assert.ok(markup.includes('id="point-size" value="4"'), 'pointSize default');
    assert.ok(markup.includes('id="zoom-input" value="100"'), 'zoomInput default');
    assert.ok(markup.includes('id="custom-page-width" value="21"'), 'customPageWidth default');
    assert.ok(markup.includes('id="custom-page-height" value="29.7"'), 'customPageHeight default');
});

test('every line/point style control carries a visible caption', () => {
    // The two swatches default to the same yellow and the two number fields are bare digits,
    // so with tooltips alone there is nothing on screen telling them apart. Each needs a label
    // bound by for= (which also makes clicking the caption focus/open that input). Captions
    // mirror the desktop toolbar's, so the two apps read the same.
    const controls = [
        ['line-color', 'Color', 'color'],
        ['point-color', 'Color', 'color'],
        ['line-thickness', 'Thickness', 'number'],
        ['point-size', 'Size', 'number'],
    ];
    for (const [id, caption, type] of controls) {
        const label = new RegExp(`<label for="${id}"[^>]*>${caption}</label>\\s*<input type="${type}" id="${id}"`);
        assert.match(markup, label, `${id} is preceded by its "${caption}" caption`);
    }
});

test('line and point styling are two separate captioned sections', () => {
    // The bare "Color" captions only disambiguate because each sits under its own section
    // label — so the split itself is the contract, not just decoration. Line owns the colour,
    // thickness and dash style; Point owns the point colour and size.
    const section = (label) => {
        const at = markup.indexOf(`<div class="ctrl-section-label">${label}</div>`);
        assert.notStrictEqual(at, -1, `a "${label}" section exists`);
        return markup.slice(at, markup.indexOf('</div>', markup.indexOf('</div>', at) + 1));
    };
    const line = section('Line');
    const point = section('Point');
    for (const id of ['line-color', 'line-thickness', 'line-style']) {
        assert.ok(line.includes(`id="${id}"`), `${id} lives in the Line section`);
        assert.ok(!point.includes(`id="${id}"`), `${id} is not in the Point section`);
    }
    for (const id of ['point-color', 'point-size']) {
        assert.ok(point.includes(`id="${id}"`), `${id} lives in the Point section`);
        assert.ok(!line.includes(`id="${id}"`), `${id} is not in the Line section`);
    }
});

// The toolbar's clusters, in the one order both surfaces build them (desktop:
// mainWindowToolbar.cpp — Image · Description & attributes · Projects · Connections & chat ·
// Edit / Line · Point / Draw · View / Zoom · Page · Formula · Data · Settings).
test('the sections are the same set, in the same order, as the desktop toolbar rows', () => {
    const order = ['Image', 'Description &amp; attributes', 'Projects', 'Connections &amp; chat',
                   'Edit', 'Line', 'Point', 'Draw', 'View', 'Zoom', 'Page', 'Formula', 'Data',
                   'Settings'];
    const at = order.map((label) => {
        const i = markup.indexOf(`<div class="ctrl-section-label">${label}</div>`);
        assert.notStrictEqual(i, -1, `a "${label}" section exists`);
        return i;
    });
    for (let i = 1; i < at.length; i++)
        assert.ok(at[i] > at[i - 1], `${order[i]} comes after ${order[i - 1]}`);
    // …and f(x,y) belongs to FORMULA, not to PAGE: inside Page, every toggle of the pill
    // resized that section and re-flowed the whole wrapping row around it.
    const formula = markup.slice(at[11], at[12]);
    const page = markup.slice(at[10], at[11]);
    for (const id of ['allow-formulas', 'formula-inputs', 'formula-x', 'formula-y', 'formula-error'])
        assert.ok(formula.includes(`id="${id}"`) && !page.includes(`id="${id}"`),
                  `${id} lives in the Formula section`);
    for (const id of ['page-size', 'unit-select', 'custom-page-width'])
        assert.ok(page.includes(`id="${id}"`), `${id} stays in Page`);
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

// The "?" badge has its OWN hover bubble (.hints-popup). A `title` on it as well means the
// same text appears twice at once — in the bubble and in the floating #app-tooltip — so it
// carries neither a title nor a data-title, and opts out of the shared tooltip explicitly.
test('the ? hints badge owns its bubble and opts out of the floating tooltip', () => {
    const badge = markup.slice(markup.indexOf('id="hints-btn"'));
    const openTag = badge.slice(0, badge.indexOf('>'));
    assert.ok(/data-no-tooltip/.test(openTag), '#hints-btn opts out of the shared tooltip');
    assert.ok(!/\stitle=/.test(openTag) && !/data-title=/.test(openTag),
        'no second copy of the shortcut text on the badge itself');
    const src = readFileSync(new URL('../js/ui/toolbar.js', import.meta.url), 'utf8');
    assert.ok(!/hintsBtn\.title\s*=/.test(src), 'and the toggle handler must not put one back');
    const tt = readFileSync(new URL('../js/ui/controlTooltip.js', import.meta.url), 'utf8');
    assert.ok(/data-no-tooltip/.test(tt), 'controlTooltip honours the opt-out');
});

// The "?" is the home for two facts only — the image size and, in incognito, that the
// session is never saved. It sits INSIDE the project-name field (which shrink-wraps its
// name) so it reads as belonging to this project instead of floating off in the toolbar.
test('the ? badge sits inside the project-name field, right after the name', () => {
    const field = markup.slice(markup.indexOf('class="project-name-field"'));
    const inField = field.slice(0, field.indexOf('id="controls-body"'));
    assert.ok(inField.indexOf('id="hints-btn"') > 0, 'the ? escaped the project-name field');
    assert.ok(inField.indexOf('id="project-name-input"') < inField.indexOf('id="hints-btn"'),
        'the ? must follow the name it belongs to');
    // The field shrink-wraps (no fixed 240px basis) — that is what makes "beside" true.
    const fieldTag = field.slice(0, field.indexOf('>'));
    assert.ok(!/flex:0 1 240px/.test(fieldTag), 'the fixed-width slot pushed the ? away again');
    assert.ok(/flex:0 1 auto/.test(fieldTag), 'the field no longer shrink-wraps its content');
});

test('the ? bubble carries the size and the incognito line — and nothing else', () => {
    const src = readFileSync(new URL('../js/ui/toolbar.js', import.meta.url), 'utf8');
    // The old shortcut wall is gone; those hints live in the ℹ info modal (infoConfig.json).
    assert.ok(!/SHORTCUTS_HINT/.test(src), 'the shortcut wall is back in the bubble');
    assert.ok(!/Ctrl\+Scroll|Alt\+Scroll|for full help/.test(src), 'shortcut text is back in the bubble');
    const info = readFileSync(new URL('../js/config/infoConfig.json', import.meta.url), 'utf8');
    for (const hint of ['Ctrl + wheel', 'Alt + wheel', 'Ctrl + Shift + wheel'])
        assert.ok(info.includes(hint), `${hint} must still be documented in the info modal`);
    // Two facts: the #image-info size line, plus an incognito line off body.incognito-mode.
    assert.match(src, /popup\.textContent = hasImage \? size : 'No image loaded';/);
    assert.match(src, /hints-incognito/);
    assert.match(src, /incognito-mode/);
    // Shown when the facts mean something — an image is open…
    assert.match(src, /\/\^Image Size:\/\.test\(size\)/);
    // …or while incognito is on, image or not (an EMPTY incognito editor used to say
    // nothing anywhere at all) — AND only while the toolbar is collapsed, since the info
    // line under the open toolbar already carries the same two facts. That line now folds
    // away with the rows, so the bubble is what is left.
    assert.match(src, /const live = \(hasImage \|\| incognito\) && collapsed;/);
    assert.match(src, /classList\.contains\('controls-collapsed'\)/);
    const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
    assert.match(css, /body\.controls-collapsed \.info \{ display: none; \}/,
      'the size line folds away with the rows it belongs to');
    // The bubble reads the info line's own text, never its incognito tag as well.
    assert.match(src, /el\.dataset\.size/);
});

// The mode has to read where the image facts are read, empty editor included: the info
// line carries its own tag (drawingApp.updateInfo), beside the "?" bubble's line.
test('the info line carries the incognito tag, and keeps its size text separable', () => {
    const app = readFileSync(new URL('../js/core/drawingApp.js', import.meta.url), 'utf8');
    const fn = app.slice(app.indexOf('  updateInfo() {'), app.indexOf('\n  }', app.indexOf('  updateInfo() {')));
    assert.match(fn, /info\.dataset\.size = info\.textContent;/, 'the size stays readable on its own');
    assert.match(fn, /class[Nn]ame = 'info-incognito'/, 'the tag is an ELEMENT, so it survives no text rewrite');
    assert.match(fn, /Incognito — not saved/);
    // The divider is built WITH the tag, so it can never appear alone.
    assert.match(fn, /class[Nn]ame = 'info-divider'/);
    const pair = fn.slice(fn.indexOf("if (this.storage.incognito)"));
    assert.ok(pair.indexOf("'info-divider'") > -1 && pair.indexOf("'info-incognito'") > -1,
        'both live inside the one incognito branch');
    assert.match(pair, /info\.append\(sep, tag\)/, 'and they are appended together');
    // The app's own glyph, not an emoji.
    assert.match(fn, /icon\('incognito', \{ size: 13 \}\)/);
    assert.ok(!/🕶/.test(fn), 'the emoji is gone from the info line');
    assert.match(fn, /this\.storage\.incognito/, 'off the one state flag');
    // Toggling the mode repaints the line (the toggle only ever calls updateIncognitoUI).
    const ui = app.slice(app.indexOf('  updateIncognitoUI() {'), app.indexOf('\n  }', app.indexOf('  updateIncognitoUI() {')));
    assert.match(ui, /this\.updateInfo\(\)/);
    const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
    assert.match(css, /\.info-incognito \{/, 'and it is styled like the bubble line');
    // The glyph sits on the words, and the divider is muted + unselectable.
    const tag = css.slice(css.indexOf('.info-incognito {'), css.indexOf('}', css.indexOf('.info-incognito {')));
    assert.match(tag, /display: inline-flex/);
    assert.match(tag, /gap: 5px/);
    assert.match(tag, /color: var\(--accent\)/, 'the glyph inherits this via currentColor');
    const div = css.slice(css.indexOf('.info-divider {'), css.indexOf('}', css.indexOf('.info-divider {')));
    assert.match(div, /color: var\(--text-muted\)/, 'muted, not competing with the facts');
    assert.match(div, /user-select: none/);
    // …and the "?" bubble carries the same glyph, not the emoji.
    const bar = readFileSync(new URL('../js/ui/toolbar.js', import.meta.url), 'utf8');
    assert.match(bar, /line\.innerHTML = `\$\{icon\('incognito', \{ size: 13 \}\)\}/);
    assert.ok(!/🕶/.test(bar), 'the emoji is gone from the bubble too');
    const hints = css.slice(css.indexOf('.hints-incognito {'), css.indexOf('}', css.indexOf('.hints-incognito {')));
    assert.match(hints, /display: flex/);
    assert.match(hints, /gap: 5px/);
});

// The frame belongs to the EDITOR, so it traces the whole visible canvas region — the
// picture AND the empty ground beside it — at any zoom and scroll offset. Drawn around
// the image alone it read as a selection round the picture (user report); pinned to the
// shrink-wrapped container it also drew a 300×150 stub with no image at all.
test('the incognito frame traces the canvas VIEWPORT, not the picture', () => {
    const markup = readFileSync(new URL('../js/ui/mainContent.js', import.meta.url), 'utf8');
    // It lives in the VIEWPORT (the scrollport), not in the shrink-wrapping container.
    const vpAt = markup.indexOf('id="canvas-viewport"');
    const frameAt = markup.indexOf('class="incognito-frame"');
    const containerAt = markup.indexOf('id="canvas-container"');
    assert.ok(vpAt > -1 && frameAt > vpAt && frameAt < containerAt,
        'the frame is a child of the viewport, ahead of the canvas container');
    const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
    const frame = css.slice(css.indexOf('.incognito-frame {'), css.indexOf('}', css.indexOf('.incognito-frame {')));
    // Sticky: an absolute box scrolls away with the content, which is what the frame must
    // NOT do — the viewport edge is the same edge however far the picture is scrolled.
    assert.match(frame, /position: sticky/);
    assert.match(frame, /top: 0/);
    assert.match(frame, /left: 0/);
    // Sized in PIXELS from the viewport's own measurements: a percentage resolves against
    // the scrollable content, i.e. the image's box at the current zoom.
    assert.match(frame, /width: var\(--vp-w, 100%\)/);
    assert.match(frame, /height: var\(--vp-h, 100%\)/);
    // …and its height is given back, so nothing in the viewport shifts.
    assert.match(frame, /margin-bottom: calc\(-1 \* var\(--vp-h, 0px\)\)/);
    assert.match(frame, /pointer-events: none/);
    // The publisher: the viewport's INNER box (less any scrollbar), on resize only —
    // sticky already handles scrolling, so nothing runs per scroll frame.
    assert.match(markup, /setProperty\('--vp-w', `\$\{vp\.clientWidth\}px`\)/);
    assert.match(markup, /setProperty\('--vp-h', `\$\{vp\.clientHeight\}px`\)/);
    assert.match(markup, /new ResizeObserver\(syncFrameBox\)\.observe\(vp\)/);
    assert.ok(!/addEventListener\('scroll'[^)]*syncFrameBox/.test(markup), 'no per-scroll work');
    // The old container-stretching workaround is gone with the container dependency.
    assert.ok(!css.includes('body.incognito-mode.canvas-empty .canvas-container'),
        'the empty-state stretch is obsolete now the frame owns the viewport');
    const layout = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
    const base = layout.slice(layout.indexOf('\n.canvas-container {'), layout.indexOf('}', layout.indexOf('\n.canvas-container {')));
    // …and the container still shrink-wraps the canvas: as a flex item, `flex: none` with
    // no width of its own is the inline-block's replacement (see canvasCentering.test.js).
    assert.ok(/flex: none/.test(base) && !/width:/.test(base),
        'and the container still shrink-wraps the canvas, untouched');
});

// The logo's hover rays (animations.css) live on ::before of .app-logo-wrap — SVG
// elements can't host pseudo-elements — so the wrap must exist exactly once and
// actually enclose the .app-logo svg, or the ray/pulse selectors silently match nothing.
test('the app logo sits inside its ray-layer wrap, with the accent menu beside it', () => {
    assert.strictEqual(count('class="app-logo-wrap"'), 1, 'one .app-logo-wrap');
    assert.strictEqual(count('class="app-logo"'), 1, 'one .app-logo');
    const wrapAt = markup.indexOf('class="app-logo-wrap"');
    const logoAt = markup.indexOf('class="app-logo"', wrapAt);
    const menuAt = markup.indexOf('class="accent-dd-menu logo-accent-menu"', wrapAt);
    const closeAt = markup.indexOf('</span>', wrapAt);
    assert.ok(wrapAt !== -1 && logoAt !== -1 && menuAt !== -1 && closeAt !== -1,
        'wrap, logo, accent menu and closing tag present');
    assert.ok(wrapAt < logoAt && logoAt < menuAt && menuAt < closeAt,
        '.app-logo and .logo-accent-menu are children of .app-logo-wrap');
    // Starts hidden and empty (accentPicker.js fills it lazily on first open).
    const menuTag = markup.slice(menuAt, markup.indexOf('>', menuAt));
    assert.ok(menuTag.includes('hidden'), 'the accent menu ships hidden');
});

// The incognito tag is passive decor: toggling it must not move a pixel of the app.
// The desktop's equivalent label grew and pushed the canvas + points panel down (user
// report). Geometry is pinned in CSS so both states are the identical box; the live
// bounding-rect proof is in the browser probe (reported alongside).
test('the status row is the same box with the incognito tag and without it', () => {
    const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
    const row = css.slice(css.indexOf('\n.info {'), css.indexOf('}', css.indexOf('\n.info {')));
    // A FLEX row is what actually guarantees it: measured, an inline tag with
    // vertical-align:middle still stretched the line box by ~1.4px however tightly its
    // own height was pinned, and that is what moved the canvas. As flex items the text
    // and the tag are laid out against the row's height instead of deciding it.
    assert.match(row, /display: flex/, 'the row is not a line box');
    assert.match(row, /align-items: center/);
    // …with a floor so the empty and tagged states agree…
    assert.match(row, /line-height: 20px/);
    assert.match(row, /min-height: 20px/);
    // …and no second line to grow onto at a narrow width.
    assert.match(row, /white-space: nowrap/);
    assert.match(row, /overflow: hidden/);
    const tag = css.slice(css.indexOf('.info-incognito {'), css.indexOf('}', css.indexOf('.info-incognito {')));
    // The tag is locked to that same height rather than stretching it, and never grows
    // or shrinks as a flex item.
    assert.match(tag, /line-height: 20px/);
    assert.match(tag, /height: 20px/);
    assert.match(tag, /flex: 0 0 auto/);
    const sep = css.slice(css.indexOf('.info-divider {'), css.indexOf('}', css.indexOf('.info-divider {')));
    assert.match(sep, /flex: 0 0 auto/);
    // The glyph is a block, so it contributes no inline baseline/descender space — the
    // pixel-or-two that moved the canvas on the desktop.
    assert.match(css, /\.info-incognito \.ic \{ display: block; flex: 0 0 auto; \}/);
    // 13px inside a 20px line box: it cannot exceed what is reserved for it.
    const app = readFileSync(new URL('../js/core/drawingApp.js', import.meta.url), 'utf8');
    const size = /icon\('incognito', \{ size: (\d+) \}\)/.exec(app);
    assert.ok(size && Number(size[1]) < 20, `the glyph (${size?.[1]}px) must fit the line box`);
    // …and nothing compensates by resizing the canvas: the frame is an overlay, and no
    // rule keys the viewport off the incognito state.
    const comp = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
    assert.ok(!/body\.incognito-mode[^{]*\.canvas-(viewport|container)[^{]*\{[^}]*(height|width|margin|padding)/.test(comp),
        'incognito must not resize the canvas to make room');
});

// REGRESSION: the ring around the focused/hovered points row came out open at the top —
// an outline paints outside the border box, and the sticky column header covered that
// edge. Inset by its own width it is always whole, matching the desktop's delegate.
test('the points-table row ring is drawn inside the row, not around it', () => {
    const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
    for (const cls of ['row-highlighted', 'row-focused']) {
        const rule = css.slice(css.indexOf(`.coordinates-table tbody tr.${cls} {`),
                               css.indexOf('}', css.indexOf(`.coordinates-table tbody tr.${cls} {`)));
        assert.match(rule, /outline: 2px solid/, `${cls} still rings the row`);
        assert.match(rule, /outline-offset: -2px/, `${cls} draws that ring inside the row`);
    }
    // …and the header really is the thing that would cover it, so the rule earns its keep.
    assert.match(css, /\.coordinates-table thead th \{[^}]*position: sticky/);
});

// REGRESSION: the project-row "…" menu sat on a 184px min-width floor while its content
// needed 149, so every row ended in dead space. It is content-sized now, with a smaller
// floor for short menus and a cap so a long label cannot run away.
test('the row menus are sized to their content, not to a wide floor', () => {
    const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
    const rule = css.slice(css.indexOf('.project-menu, .chat-row-menu {'),
                           css.indexOf('}', css.indexOf('.project-menu, .chat-row-menu {')));
    assert.match(rule, /width: max-content/, 'the menu hugs its widest row');
    const floor = Number(/min-width: (\d+)px/.exec(rule)[1]);
    assert.ok(floor <= 150, `the floor (${floor}px) must not exceed what the items need`);
    assert.match(rule, /max-width: min\(/, 'and a long label is capped rather than unbounded');
});

// The selected-line bar is parted into header | colours | geometry | fill | actions by
// hairlines in its own amber. REGRESSION: the fill group's separator stayed when the
// group hid, leaving two hairlines with nothing between them. It comes and goes with it.
test('the bar separators part it in four, and the fill one follows its group', () => {
    const js = readFileSync(new URL('../js/ui/selectionPanel.js', import.meta.url), 'utf8');
    const inner = js.slice(js.indexOf('static inner()'), js.indexOf('static template()'));
    assert.equal((inner.match(/class="sel-sep"/g) || []).length, 4, 'four separators');
    // The fill group's own one is identified, starts hidden, and is toggled with the group.
    assert.match(inner, /id="sel-fill-sep"[^>]*style="display:none;"/);
    // …through the same slide+dust the group itself uses (selectionPanelMotion covers the
    // motion; here it is only that the separator is driven WITH the group, never alone).
    const show = js.slice(js.indexOf('const fillSep ='), js.indexOf('const panel ='));
    assert.match(show, /revealControls\(fillSep, true, 'block'\)/, 'shown with the group');
    assert.match(show, /revealControls\(fillSep, false\)/, 'and hidden with it');
    // Deselect wears the bar's own amber token, not an orange literal of its own.
    const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
    const cta = css.slice(css.indexOf('.deselect-btn {'), css.indexOf('}', css.indexOf('.deselect-btn {')));
    assert.match(cta, /var\(--bg-sel-btn\)/, 'the same token its sibling buttons use');
    assert.ok(!/#e67e22/.test(cta), 'and no hardcoded orange left');
});

// Only the custom tooltip (ui/controlTooltip.js) ever shows: no control carries a native
// `title` — authored in markup or assigned from code — anywhere in the app or the extension
// (user report: the toolbar mic showed both). Text lives in data-title, composed text
// (shortcut + disabled reason) in data-tip; the tooltip reads those and nothing else.
test('no native title attribute anywhere — the custom tooltip is the only tooltip', async () => {
  const { readFileSync, readdirSync, statSync } = await import('node:fs');
  const { join } = await import('node:path');
  const walk = (dir, out = []) => {
    for (const f of readdirSync(dir)) {
      const p = join(dir, f);
      if (statSync(p).isDirectory()) walk(p, out);
      else if (/\.(js|html)$/.test(f)) out.push(p);
    }
    return out;
  };
  const roots = [new URL('../js', import.meta.url).pathname, new URL('../../extension/src', import.meta.url).pathname];
  const offenders = [];
  for (const file of roots.flatMap((r) => walk(r))) {
    const src = readFileSync(file, 'utf8');
    if (/[\s\n]title="/.test(src)) offenders.push(`${file}: title="`);
    // DOM setters and attribute writes; document.title (the tab) and parseTip's result object are not tooltips.
    for (const m of src.matchAll(/(?<![\w.])([A-Za-z_$][\w$]*)\.title = |setAttribute\('title'/g)) {
      if (m[1] === 'document' || m[1] === 'tip') continue;
      offenders.push(`${file}: ${m[0].trim()}`);
    }
  }
  assert.deepEqual(offenders, []);
  const ct = readFileSync(new URL('../js/ui/controlTooltip.js', import.meta.url), 'utf8');
  assert.ok(ct.includes("closest('[data-tip], [data-title]')"), 'the tooltip listens for data attributes only');
  assert.ok(!ct.includes("getAttribute('title')"), 'and never reads the native one');
});
