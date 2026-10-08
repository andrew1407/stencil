// Dragging one colour swatch onto another copies its colour: past the press slop a chip of the
// colour rides beside the pointer, every swatch it can land on glows (the one under it brighter), and
// the one it is released on takes it through its own change path. A swatch is wired on its first
// press, so rows and dialogs built later join. Desktop twin: desktop/src/support/drag/colorDrag.{hpp,cpp}.
import { wireIconDrag, markDropTarget } from './iconDrag.js';
import { swatchOf, liveSwatches, colorToApply, liftableColor, bindSwatchApp } from './colorDragSwatches.js';

export const CHIP_CLASS = 'color-drag-chip';
export const CHIP_OFFSET_PX = 12;   // right of and below the pointer, so the swatch under it stays in view
// A swatch under an open window's backdrop cannot be reached, so a drag stays in its own layer.
const layerOf = (el) => el.closest?.('.app-modal-overlay') ?? null;

const createChip = (color) => {
  const chip = document.createElement('div');
  chip.className = CHIP_CLASS;
  chip.setAttribute('aria-hidden', 'true');
  chip.style.setProperty('--chip-color', color);
  document.body.appendChild(chip);
  return {
    move(x, y) {
      chip.style.left = `${x + CHIP_OFFSET_PX}px`;
      chip.style.top = `${y + CHIP_OFFSET_PX}px`;
    },
    destroy() { chip.remove(); },
  };
};

/** One swatch's drag hooks for wireIconDrag: lift its colour, light the targets, drop on one. */
export const colorDragHooks = (el, { chipFor = createChip, targets = liveSwatches } = {}) => {
  let source = null;
  let chip = null;
  let lit = [];
  let over = null;
  const end = () => {
    chip?.destroy();
    chip = null;
    for (const t of lit) markDropTarget(t.el, false);
    lit = [];
    over = null;
  };
  // Only a swatch lit at the start takes the drop: shown, enabled and not the source.
  const targetAt = (node) => {
    const hit = node ? swatchOf(node) : null;
    return hit ? lit.find((t) => t.el === hit.el) ?? null : null;
  };
  return {
    start: (p) => {
      source = swatchOf(el);
      const color = source?.enabled() ? source.read() : null;
      if (!liftableColor(color)) return false;
      chip = chipFor(color);
      chip.move(p.x, p.y);
      lit = targets().filter((t) => t.el !== source.el && layerOf(t.el) === layerOf(source.el));
      for (const t of lit) markDropTarget(t.el, true);
      return true;
    },
    move: (p) => {
      chip?.move(p.x, p.y);
      const t = targetAt(p.target);
      if (t === over) return;
      if (over) markDropTarget(over.el, true, false);
      over = t;
      if (t) markDropTarget(t.el, true, true);
    },
    drop: (p) => {
      const t = targetAt(p.target);
      const from = source;
      end();
      const color = t ? colorToApply(from, t) : null;
      if (color) t.apply(color);
    },
    cancel: end,
  };
};

/** Every colour swatch under `root` becomes a drag source the first time it is pressed. */
export const wireColorDrag = (app, root = document) => {
  bindSwatchApp(app);
  const wired = new WeakSet();
  root.addEventListener('pointerdown', (e) => {
    const s = e.target?.nodeType === 1 ? swatchOf(e.target) : null;
    if (!s || wired.has(s.el)) return;
    wired.add(s.el);
    // Added during this press's capture phase, so the press itself reaches it at the swatch.
    wireIconDrag(s.el, { ...colorDragHooks(s.el), ghost: false, enabled: () => !!swatchOf(s.el)?.enabled() });
  }, true);
};
