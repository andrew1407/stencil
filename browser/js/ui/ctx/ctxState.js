import { setHtml, setRadioGroup, formatCombo, hasAnyLines } from '../../utils.js';
import { hotkeys } from '../../core/hotkeys.js';
import { icon } from '../icons.js';
import { keysHtml } from '../tip/tipContent.js';
import { revealControls } from '../motion.js';
import { EXPORT_VARIANTS, exportVariantState } from '../export/exportVariants.js';

// Reflect the app's live state into the open context menu, polled every 120ms; setHtml
// writes only on a real change, so a hovered row's glyph is never rebuilt under the pointer.
export const ctxSyncState = (app) => {
    if (!app) return;
    document.getElementById('ctx-draw-label').textContent =
      app.isDrawing ? 'Stop Drawing' : 'Start Drawing';
    setHtml(document.getElementById('ctx-draw-toggle').querySelector('.ctx-icon'),
      app.isDrawing ? icon('stop', { size: 14 }) : icon('play', { size: 14 }));

    // An unavailable row is hidden, not greyed: the menu has no tooltip to explain a
    // disabled row, and it is live-synced, so the row reappears as soon as it is possible.
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
    // Variant-row availability and the primary-combo owner come from exportVariants.js.
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
    gate('ctx-share-img', !hasImage);
    gate('ctx-paste-layout', !hasImage);

    hotkeys.updateCtxHints();
    // Overridden after updateCtxHints so it reflects the start/stop binding for the live state.
    const drawCombo = hotkeys.get(app.isDrawing ? 'stopDraw' : 'startDraw');
    setHtml(document.getElementById('ctx-draw-hotkey'),
      keysHtml(formatCombo(drawCombo, hotkeys.isMac), hotkeys.isMac));

    setHtml(document.getElementById('ctx-chk-points'), app.showPoints ? icon('check', { size: 14 }) : '');
    setHtml(document.getElementById('ctx-chk-lines'), app.showLines  ? icon('check', { size: 14 }) : '');

    document.getElementById('ctx-point-size').value = app.pointSize;
    document.getElementById('ctx-thickness').value = app.thickness;
    setRadioGroup('ctxLineStyle', app.style);

    setRadioGroup('ctxFilter', app.imageFilter);
    document.getElementById('ctx-tint-color').value = app.filterColor || '#7c3aed';
    const tintRow = document.getElementById('ctx-tint-row');
    tintRow.classList.toggle('ctx-tint-visible', app.imageFilter === 'custom');

    const isFS = document.body.classList.contains('fullscreen-mode');
    document.getElementById('ctx-fs-label').textContent = isFS ? 'Exit Fullscreen' : 'Enter Fullscreen';
    setHtml(document.getElementById('ctx-fullscreen').querySelector('.ctx-icon'), icon(isFS ? 'minimize' : 'maximize'));

    document.getElementById('ctx-tt-enabled').checked = app.tooltipEnabled;
    document.getElementById('ctx-tt-page').checked = app.tooltipShowPage;
    document.getElementById('ctx-tt-screen').checked = app.tooltipShowScreen;
    document.getElementById('ctx-tt-coords').checked = app.tooltipShowCoords;
    document.getElementById('ctx-allow-formulas').checked = app.allowFormulas;
    revealControls(document.getElementById('ctx-formula-inputs'), app.allowFormulas, 'block');
};
