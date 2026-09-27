import { readColorPair, notify } from '../../utils.js';
import { applySelectionChange } from '../../core/line/selection.js';
import { applyFill } from '../panel/selectionPanel.js';
export function wireSelectionPanelControls(app) {
  // The swatch and its opacity box are ONE colour (readColorPair: a color input has no alpha
  // byte). `input` previews it; the trailing `change` commits one undo step.
  const wireColorPair = (colorId, alphaId, prop) => {
    const apply = (commit) => () => applySelectionChange(app, prop, readColorPair(colorId, alphaId), { commit });
    for (const el of [document.getElementById(colorId), document.getElementById(alphaId)]) {
      el?.addEventListener('input', apply(false));
      el?.addEventListener('change', apply(true));
    }
  };
  wireColorPair('sel-color', 'sel-alpha', 'color');
  wireColorPair('sel-point-color', 'sel-point-alpha', 'pointColor');
  document.getElementById('sel-thickness').addEventListener('change', e => applySelectionChange(app, 'thickness', parseInt(e.target.value, 10)));
  document.getElementById('sel-point-size').addEventListener('change', e => applySelectionChange(app, 'pointSize', parseInt(e.target.value, 10)));
  document.getElementById('sel-style').addEventListener('change', e => applySelectionChange(app, 'style', e.target.value));
  // No on/off tick: the fill IS the swatch plus its alpha, and 0 alpha is "none" — so picking a
  // colour on an unfilled area must also raise the alpha off 0, or it applies invisibly.
  const fillAlphaBox = () => document.getElementById('sel-fill-alpha');
  const fill = document.getElementById('sel-fill');
  fill.addEventListener('input', () => {
    const a = fillAlphaBox();
    if (a && Number(a.value) <= 0) a.value = '255';
    applyFill(app, { commit: false });
  });
  fill.addEventListener('change', () => applyFill(app));
  fillAlphaBox()?.addEventListener('input', () => applyFill(app, { commit: false }));
  fillAlphaBox()?.addEventListener('change', () => applyFill(app));
  document.getElementById('sel-fill-clear').addEventListener('click', () => {
    const a = fillAlphaBox();
    if (a) a.value = '0';
    applyFill(app);
    notify('Fill cleared (transparent)', 'ok');
  });
  document.getElementById('sel-unchain').addEventListener('click', () => app.unchainSelectedLine());
  document.getElementById('sel-deselect').addEventListener('click', () => app.deselectLine());
}
