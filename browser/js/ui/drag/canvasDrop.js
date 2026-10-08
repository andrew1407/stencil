// A toolbar action dropped on the canvas: while the drag is live the canvas frame glows as its
// target, brighter under the pointer; released over it the action runs, anywhere else nothing
// does. Desktop twin: installCanvasDrag in desktop/src/app/drag/toolbarDrags.{hpp,cpp}.
import { wireIconDrag, markDropTarget } from './iconDrag.js';

// `frame()` names the canvas frame when the drag starts (none: no drag); `act` is the drop's work.
export const canvasDropHooks = ({ frame, act }) => {
  let target = null;
  const over = (p) => !!target && !!p.target && (p.target === target || !!target.contains?.(p.target));
  const end = () => { markDropTarget(target, false); target = null; };
  return {
    start: () => {
      target = frame();
      if (!target) return false;
      markDropTarget(target, true);
      return true;
    },
    move: (p) => markDropTarget(target, true, over(p)),
    drop: (p) => {
      const hit = over(p);
      end();
      if (hit) act();
    },
    cancel: end,
  };
};

export const wireCanvasDrop = (btn, opts) => (btn ? wireIconDrag(btn, canvasDropHooks(opts)) : null);
