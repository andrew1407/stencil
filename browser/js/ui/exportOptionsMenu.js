// ── Copy/Download-image toolbar options ──────────────────────────────────
// The copy-image and save-image toolbar buttons keep their plain click (runs the
// PRIMARY variant — 'current', or 'split' while a split compare view is active), and
// gain a small dust-animated options list (js/ui/dropdownMenu.js) for the other variants
// — opened by double-click, right-click, or an Alt+hover peek, the SAME gesture every
// modal-opening toolbar icon already uses (js/ui/popover.js). Alt+hover over a row
// previews it (js/ui/exportPreview.js).
import { showMenu, hideMenu } from './dropdownMenu.js';
import { wireModalOpenGestures } from './popover.js';
import { wireAltPreview, hideExportPreview, clearAltPreviewHover } from './exportPreview.js';
import { icon } from './icons.js';
import { keysHtml } from './tipContent.js';
import { hotkeys } from '../core/hotkeys.js';
import { formatCombo } from '../utils.js';
import { EXPORT_VARIANTS, EXPORT_VARIANT_LABELS, EXPORT_VARIANT_ICONS,
         exportVariantState } from './exportVariants.js';

// `run(variant)` performs the action (copy or download); `currentIcon` is the glyph
// name for the 'current' row (the trigger's own action); `hotkeyIds` maps a variant to
// its hotkeysConfig.json id, live and platform-formatted (copy has one per variant;
// download only has one, for 'current' — a row with no id mapped, or no live binding,
// just shows no hotkey, same as the context menu's own download-variant rows).
// Rows, order, and availability come from the shared registry (exportVariants.js),
// including the "With Compare is a FOURTH row" rule.
export function wireExportOptionsMenu(trigger, app, { run, currentIcon = 'copy', hotkeyIds = {} } = {}) {
  if (!trigger) return;

  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu';
  menu.setAttribute('role', 'listbox');
  menu.hidden = true;
  trigger.insertAdjacentElement('afterend', menu);

  // Whichever variant the PRIMARY gesture (this trigger's own combo, plain click)
  // means right now.
  const primaryVariant = () => exportVariantState(app).primary;

  // Reuses the context menu's INNER row shape (.ctx-icon / .ctx-label / .ctx-hotkey) so
  // the alignment CSS and the hover shake apply here too (components.css scopes both to
  // ".accent-dd-opt, .ctx-item" — one rule, not a second copy) — the row itself stays
  // .accent-dd-opt (this dropdown's own base flex/padding/hover), not .ctx-item, so the
  // two rules never fight the cascade over which one wins.
  const build = () => {
    menu.innerHTML = '';
    const state = exportVariantState(app);
    for (const v of EXPORT_VARIANTS.filter((x) => state.show[x])) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'option');
      // 'current' and 'split' SHARE one physical shortcut — only whichever of the two
      // is primary right now shows it. 'original'/'tint' each carry their OWN dedicated
      // combo, always shown regardless of primary/compare state.
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
