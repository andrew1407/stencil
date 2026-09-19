import { markSwap } from '../motion.js';
import { showMenu, hideMenu } from '../dropdownMenu.js';
export function wireZoomControls(app) {
  const zoomInput = document.getElementById('zoom-input');
  // `commit` (change/Enter/preset-pick) reverts an out-of-range value; live typing just skips
  // invalid values. Full 5%–3200% range — the input max, the presets and core clampScale.
  const applyZoomInput = (commit = false) => {
    const val = parseFloat(zoomInput.value);
    if (!isNaN(val) && val >= 5 && val <= 3200) {
      app.zoomPan.zoomAroundCenter(val / 100);
    } else if (commit) {
      zoomInput.value = Math.round(app.scale * 100);
    }
  };
  // Live update as you type (matches the desktop combobox, which applies on every keystroke),
  // plus a committing pass on change/Enter that also snaps back an invalid final value.
  zoomInput.addEventListener('input', () => applyZoomInput(false));
  zoomInput.addEventListener('change', () => applyZoomInput(true));

  // ── Preset dropdown: opens on focus/click, so the input offers BOTH free typing and
  // one-click preset selection (the native datalist on number inputs is unreliable). ──
  const ZOOM_PRESETS = [10, 25, 50, 75, 100, 125, 150, 200, 300, 400, 500, 800, 1600, 3200];
  const zoomMenu = document.getElementById('zoom-menu');
  // Ported to the shared dropdown machinery (js/ui/dropdownMenu.js): same portal,
  // placement and particle-dust open/close every other popup uses. Markup unchanged.
  const openZoomMenu = () => {
    if (!zoomMenu || zoomInput.disabled || !zoomMenu.hidden) return;
    const cur = Math.round(app.scale * 100);
    zoomMenu.innerHTML = ZOOM_PRESETS.map(p =>
      `<div class="zoom-menu-item${p === cur ? ' active' : ''}" data-val="${p}" role="option">${p}%</div>`).join('');
    showMenu(zoomMenu, zoomInput);
  };
  const closeZoomMenu = () => { if (zoomMenu && !zoomMenu.hidden) hideMenu(zoomMenu); };
  zoomInput.addEventListener('focus', openZoomMenu);
  zoomInput.addEventListener('click', openZoomMenu);
  if (zoomMenu) {
    // mousedown (not click) so it fires before the input's blur hides the menu.
    zoomMenu.addEventListener('mousedown', e => {
      const item = e.target.closest('.zoom-menu-item');
      if (!item) return;
      e.preventDefault();
      // A preset pick is a value EXCHANGE like a select's (customSelect markSwap); typing stays
      // plain, so this fires only from the dropped list.
      const to = item.dataset.val;
      if (zoomInput.value !== to) markSwap(zoomInput, () => { zoomInput.value = to; });
      else zoomInput.value = to;
      applyZoomInput(true);
      closeZoomMenu();
      zoomInput.blur();
    });
  }
  zoomInput.addEventListener('blur', () => setTimeout(closeZoomMenu, 120));
  // The open menu now lives on <body> (showMenu), so "outside" has to miss it too, or
  // a click on its own padding (not a .zoom-menu-item row) would count as outside.
  document.addEventListener('click', e => {
    if (!e.target.closest('.zoom-input-wrap') && !e.target.closest('.zoom-menu')) closeZoomMenu();
  });

  zoomInput.addEventListener('keydown', e => {
    if (e.key === 'Enter') { e.preventDefault(); applyZoomInput(true); closeZoomMenu(); zoomInput.blur(); }
    if (e.key === 'Escape') { zoomInput.value = Math.round(app.scale * 100); closeZoomMenu(); zoomInput.blur(); }
  });
  // Prevent zoom input scroll from zooming the canvas
  zoomInput.addEventListener('wheel', e => e.stopPropagation());
}
