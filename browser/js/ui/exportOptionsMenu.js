// The copy/download-image toolbar buttons keep their plain click (the primary variant) and
// gain a dust-animated options list for the others, opened by the same gestures every
// modal-opening toolbar icon uses (popover.js). Alt+hover previews a row (exportPreview.js).
import { showMenu, hideMenu } from './dropdownMenu.js';
import { wireModalOpenGestures } from './popover.js';
import { wireAltPreview, hideExportPreview, clearAltPreviewHover } from './exportPreview.js';
import { icon } from './icons.js';
import { keysHtml } from './tipContent.js';
import { hotkeys } from '../core/hotkeys.js';
import { formatCombo } from '../utils.js';
import { EXPORT_VARIANTS, EXPORT_VARIANT_LABELS, EXPORT_VARIANT_ICONS,
         exportVariantState } from './exportVariants.js';

// `hotkeyIds` maps a variant to its hotkeysConfig.json id (a row with no live binding shows
// none); rows, order and availability come from exportVariants.js.
export function wireExportOptionsMenu(trigger, app, { run, currentIcon = 'copy', hotkeyIds = {} } = {}) {
  if (!trigger) return;

  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu';
  menu.setAttribute('role', 'listbox');
  menu.hidden = true;
  trigger.insertAdjacentElement('afterend', menu);

// Whichever variant the plain click means right now.
  const primaryVariant = () => exportVariantState(app).primary;

// Reuses the context menu's inner row shape (.ctx-icon / .ctx-label / .ctx-hotkey) so
// components/ctxAssistant.css covers both; the row itself stays .accent-dd-opt.
  const build = () => {
    menu.innerHTML = '';
    const state = exportVariantState(app);
    for (const v of EXPORT_VARIANTS.filter((x) => state.show[x])) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'option');
// 'current' and 'split' share one shortcut — only the primary one shows it; 'original'
// and 'tint' carry their own.
      const hkId = v === 'split' ? hotkeyIds.current : hotkeyIds[v];
      const sharesPrimarySlot = v === 'current' || v === 'split';
      const combo = hkId && (!sharesPrimarySlot || v === state.primary) && hotkeys.get(hkId);
      li.innerHTML = `<span class="ctx-icon">${icon(v === 'current' ? currentIcon : EXPORT_VARIANT_ICONS[v])}</span>` +
        `<span class="ctx-label">${EXPORT_VARIANT_LABELS[v]}</span>` +
        (combo ? `<span class="ctx-hotkey">${keysHtml(formatCombo(combo, hotkeys.isMac), hotkeys.isMac)}</span>` : '');
      li.addEventListener('click', () => { close(); run(v); });
      wireAltPreview(li, app, v);
      menu.appendChild(li);
    }
  };

  const onDocDown = (e) => { if (!trigger.contains(e.target) && !menu.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') close(); };

  const openPopover = () => {
    if (!app.image) return;
    build();
    showMenu(menu, trigger);
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey);
  };
  const close = () => {
    if (menu.hidden) return;
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey);
    hideMenu(menu);
    hideExportPreview();
    clearAltPreviewHover();
  };

  wireModalOpenGestures(trigger, {
    openFull: () => run(primaryVariant()),
    openPopover,
    closePopover: close,
    isPopoverOpen: () => !menu.hidden,
  });
}
