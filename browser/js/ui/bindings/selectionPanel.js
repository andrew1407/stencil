import { readColorPair, notify } from '../../utils.js';
export function wireSelectionPanelControls(app) {
  // The swatch and its opacity box are two halves of ONE colour, joined by utils.js's
  // readColorPair — <input type="color"> cannot carry an alpha byte of its own.
  const wireColorPair = (colorId, alphaId, prop) => {
    const apply = () => app.applySelectionChange(prop, readColorPair(colorId, alphaId));
    document.getElementById(colorId).addEventListener('input', apply);
    document.getElementById(alphaId)?.addEventListener('input', apply);
  };
  wireColorPair('sel-color', 'sel-alpha', 'color');
  wireColorPair('sel-point-color', 'sel-point-alpha', 'pointColor');
  document.getElementById('sel-thickness').addEventListener('change', e => app.applySelectionChange('thickness', parseInt(e.target.value, 10)));
  document.getElementById('sel-point-size').addEventListener('change', e => app.applySelectionChange('point-size', parseInt(e.target.value, 10)));
  document.getElementById('sel-style').addEventListener('change', e => app.applySelectionChange('style', e.target.value));
  // No on/off tick: the fill IS the swatch plus its alpha, and 0 alpha is "none".
  // Picking a colour on an unfilled area therefore also has to raise the alpha off 0,
  // or the choice would apply invisibly.
  const fillAlphaBox = () => document.getElementById('sel-fill-alpha');
  document.getElementById('sel-fill').addEventListener('input', () => {
    const a = fillAlphaBox();
    if (a && Number(a.value) <= 0) a.value = '255';
    app.applyFill();
  });
  fillAlphaBox()?.addEventListener('input', () => app.applyFill());
  document.getElementById('sel-fill-clear').addEventListener('click', () => {
    const a = fillAlphaBox();
    if (a) a.value = '0';
    app.applyFill();
    notify('Fill cleared (transparent)', 'ok');
  });
  document.getElementById('sel-unchain').addEventListener('click', () => app.unchainSelectedLine());
  document.getElementById('sel-deselect').addEventListener('click', () => app.deselectLine());
}
