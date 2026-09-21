// The context menu's Stencil Script entry: gating + wiring (the flyout's editor is
// editor.js). The script WINDOW stays exactly as it was — only what this row does
// changes: it now opens a compact editor beside the menu instead of the full window.
import { isTouchLike, onWindowResize } from '../../utils.js';
import { scriptFlyoutHtml } from './scriptItem.js';
import { wireCtxScriptEditor } from './scriptEditor.js';

export const wireCtxScript = (app, host) => {
  // The flyout is not a list of .ctx-items, so none of the "activate → closeMenu()"
  // wiring applies: typing, running and failing leave the menu open.
  let scriptWired = false;
  let dropEditor = () => {};
  // The phone/touch fallback: the full window, through its one opener.
  const openScriptWindow = () => { document.getElementById('script-btn')?.click(); };

  // Runs on every open and on a resize, so a mode change between opens needs no reload.
  const syncScript = () => {
    const item = document.getElementById('ctx-script');
    if (!item) return;
    if (!scriptWired) {
      // Hung off the static row and wired like one of the markup's own submenu parents.
      // Built once, then only hidden: a script you typed survives the menu closing on you.
      item.insertAdjacentHTML('beforeend', scriptFlyoutHtml());
      const sub = item.querySelector(':scope > .ctx-sub');
      if (!sub) return;
      scriptWired = true;
      host.wireSubmenu(item, sub);
      dropEditor = wireCtxScriptEditor(app, host, openScriptWindow);
    }
    // Plain (no flyout) on phones and coarse pointers (the app-wide touch rule, utils.js).
    const plain = isTouchLike();
    item.dataset.noSub = plain ? '1' : '0';
    item.classList.toggle('ctx-script-plain', plain);
    if (plain && document.getElementById('ctx-script-sub')?.classList.contains('ctx-sub-visible')) {
      host.closeAllSubs();
    }
  };
  // A resize while the menu is open re-evaluates the mode.
  onWindowResize(() => { if (host.menuIsOpen()) syncScript(); });
  syncScript();

  return { syncScript, dropEditor: () => dropEditor() };
};
