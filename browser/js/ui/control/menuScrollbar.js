// A drawn scrollbar for a capped menu. macOS renders its overlay bar only while the content is
// moving, so a list that scrolls read as one merely cut off; this one is there from the open.
// The thumb is a background LAYER placed by two custom properties, never a child: these menus
// address their rows as `children`. Canvas parity: ui/canvasScrollbars.js.
// Byte-pinned to browser-extension/src/lib.
import { thumbMetrics } from './thumbMetrics.js';

const INSET_PX = 3;   // the track's margin from the menu's top and bottom edges

// Writes `--dd-sb-len` / `--dd-sb-pos` for the current scroll, or clears them when the menu
// fits — the layer is sized from them, so a list that fits draws nothing.
export const syncMenuScrollbar = (menu) => {
  if (!menu) return null;
  const track = menu.clientHeight - 2 * INSET_PX;
  const m = thumbMetrics(menu.clientHeight, menu.scrollHeight, menu.scrollTop, track);
  if (!m) {
    menu.style.removeProperty('--dd-sb-len');
    menu.style.removeProperty('--dd-sb-pos');
    return null;
  }
  menu.style.setProperty('--dd-sb-len', `${m.len}px`);
  menu.style.setProperty('--dd-sb-pos', `${INSET_PX + m.pos}px`);
  return m;
};

// Keeps the thumb with the content for as long as the menu is open. Returns the detach.
export const attachMenuScrollbar = (menu) => {
  if (!menu) return () => {};
  if (menu.__ddSbOff) menu.__ddSbOff();
  const onScroll = () => syncMenuScrollbar(menu);
  menu.addEventListener('scroll', onScroll, { passive: true });
  syncMenuScrollbar(menu);
  const off = () => {
    menu.removeEventListener('scroll', onScroll);
    menu.style.removeProperty('--dd-sb-len');
    menu.style.removeProperty('--dd-sb-pos');
    menu.__ddSbOff = null;
  };
  menu.__ddSbOff = off;
  return off;
};
