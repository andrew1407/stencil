// A toolbar icon that opens a window, dragged off and released away from it: the window opens as a
// click opens it (full, never the popover), its top-left corner on the release point inside the
// viewport, flying out of the cursor and home to the icon; one already open moves there. Desktop
// twin: installDialogDrag in desktop/src/app/drag/toolbarDrags.{hpp,cpp}.
import { wireIconDrag, dropAnchor } from './iconDrag.js';
import { shellFor } from '../modal/registry.js';
import UI_STRINGS from '../../../../common/config/uiStrings.json' with { type: 'json' };

// [opener id, overlay id], one row per opener, for every window config/uiStrings.json names.
export const windowOpeners = (windows = UI_STRINGS.windows) =>
  windows.flatMap((w) => [].concat(w.opener).map((id) => [id, w.overlay]));

// `shells` resolves the window's shell at each step: one not wired yet refuses the drag.
export const modalDragHooks = (btn, overlayId, shells = shellFor) => ({
  start: () => !!shells(overlayId),
  drop: (p) => shells(overlayId)?.open(dropAnchor(p.x, p.y), btn, { at: { x: p.x, y: p.y } }),
});

// Every opener `root` holds (the toolbar, through its scoped `$`); returns the ids it wired.
export const wireModalDrags = (root) => {
  const wired = [];
  for (const [id, overlayId] of windowOpeners()) {
    const btn = root?.$?.(id);
    if (!btn || !wireIconDrag(btn, modalDragHooks(btn, overlayId))) continue;
    wired.push(id);
  }
  return wired;
};
