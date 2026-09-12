import { setHtml, setRadioGroup, formatCombo, hasAnyLines } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { keysHtml } from './tipContent.js';
import { revealControls } from './motion.js';
import { EXPORT_VARIANTS, exportVariantState } from './exportVariants.js';

// Reflect the app's live state into the open context menu — ported from contextMenu.js.
// The menu polls this every 120ms while open, so the shared setHtml (utils.js) writes only
// on a real change: a hovered row's glyph is never rebuilt under the pointer (which would
// loop its once-per-hover CSS animation).
export const ctxSyncState = (app) => {
    if (!app) return;
    document.getElementById('ctx-draw-label').textContent =
      app.isDrawing ? 'Stop Drawing' : 'Start Drawing';
    setHtml(document.getElementById('ctx-draw-toggle').querySelector('.ctx-icon'),
      app.isDrawing ? icon('stop', { size: 14 }) : icon('play', { size: 14 }));

    // A row whose action isn't available right now is HIDDEN, not greyed out — the
    // context menu carries no tooltip of its own (item 5) to explain a disabled row,
    // so a grey one just read as broken. Hiding removes the row instead of leaving a
    // dead one to puzzle over; the menu is rebuilt fresh (syncState) on every open and
    // live-synced while it's up, so a row reappears the moment its action is possible.
    const gate = (id, off) => {
      const el = document.getElementById(id);
      if (!el) return;
      el.style.display = off ? 'none' : '';
    };

    const hasLines = hasAnyLines(app);
    gate('ctx-dl-layout', !hasLines);
    gate('ctx-copy-layout', !hasLines);
    gate('ctx-clear-lines', !hasLines);

    const hasImage = !!app.image;
    gate('ctx-copy-img', !hasImage);
    gate('ctx-dl-img', !hasImage);
    // Variant-row availability and the primary-combo owner come from the shared
    // registry (exportVariants.js — the "With Compare is a FOURTH row" rule included).
    const variantState = exportVariantState(app);
    for (const v of EXPORT_VARIANTS) {
      gate(`ctx-copy-img-${v}`, !variantState.show[v]);
      gate(`ctx-dl-img-${v}`, !variantState.show[v]);
    }
    const splitPrimary = variantState.primary === 'split';
    const copyCombo = keysHtml(formatCombo(hotkeys.get('copyImage'), hotkeys.isMac), hotkeys.isMac);
    const dlCombo = keysHtml(formatCombo(hotkeys.get('saveImage'), hotkeys.isMac), hotkeys.isMac);
    setHtml(document.getElementById('ctx-copy-img-split-hk'), splitPrimary ? copyCombo : '');
    setHtml(document.getElementById('ctx-copy-img-current-hk'), splitPrimary ? '' : copyCombo);
    setHtml(document.getElementById('ctx-dl-img-split-hk'), splitPrimary ? dlCombo : '');
    setHtml(document.getElementById('ctx-dl-img-current-hk'), splitPrimary ? '' : dlCombo);
    // Share item is rendered only where Web Share files are supported (see wire()).
    gate('ctx-share-img', !hasImage);
    // Paste-layout needs an image too (matching paste handler behavior)
    gate('ctx-paste-layout', !hasImage);

    // Refresh hotkey hint text in case shortcuts were remapped
    hotkeys.updateCtxHints();
    // The draw hotkey span carries data-hk="startDraw"; override it after
    // updateCtxHints so it reflects the start/stop binding for the live state.
    const drawCombo = hotkeys.get(app.isDrawing ? 'stopDraw' : 'startDraw');
    setHtml(document.getElementById('ctx-draw-hotkey'),
      keysHtml(formatCombo(drawCombo, hotkeys.isMac), hotkeys.isMac));

    // Checkmarks
    setHtml(document.getElementById('ctx-chk-points'), app.showPoints ? icon('check', { size: 14 }) : '');
    setHtml(document.getElementById('ctx-chk-lines'), app.showLines  ? icon('check', { size: 14 }) : '');

    // Style sub values
    document.getElementById('ctx-point-size').value = app.pointSize;
    document.getElementById('ctx-thickness').value = app.thickness;
    setRadioGroup('ctxLineStyle', app.style);

    // Filter sub values
    setRadioGroup('ctxFilter', app.imageFilter);
    document.getElementById('ctx-tint-color').value = app.filterColor || '#7c3aed';
    const tintRow = document.getElementById('ctx-tint-row');
    tintRow.classList.toggle('ctx-tint-visible', app.imageFilter === 'custom');

    // Fullscreen label
    const isFS = document.body.classList.contains('fullscreen-mode');
    document.getElementById('ctx-fs-label').textContent = isFS ? 'Exit Fullscreen' : 'Enter Fullscreen';
    setHtml(document.getElementById('ctx-fullscreen').querySelector('.ctx-icon'), icon(isFS ? 'minimize' : 'maximize'));

    // Tooltip checkboxes
    document.getElementById('ctx-tt-enabled').checked = app.tooltipEnabled;
    document.getElementById('ctx-tt-page').checked = app.tooltipShowPage;
    document.getElementById('ctx-tt-screen').checked = app.tooltipShowScreen;
    document.getElementById('ctx-tt-coords').checked = app.tooltipShowCoords;
    // Formula checkbox
    document.getElementById('ctx-allow-formulas').checked = app.allowFormulas;
    revealControls(document.getElementById('ctx-formula-inputs'), app.allowFormulas, 'block');
};
