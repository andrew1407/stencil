// The projects window's ✕ as a drop target: only the project open in this tab arms it, and then
// it glows while the row is held, brightens under the pointer, and a drop there closes the project
// here at once. Desktop twin: dialogs/projects/list/ProjectDragMenu.cpp.
import { pointInRect } from '../../../utils.js';
import { markDropTarget } from '../../drag/iconDrag.js';
import { confirmCloseProject } from '../closeProject.js';

// `settle` runs once the project is closed.
export function createDragClose({ app, closeBtn, settle = () => {} }) {
  let armed = false;
  const over = (x, y) => {
    const r = closeBtn?.getBoundingClientRect?.();
    return !!r && r.width > 0 && pointInRect(x, y, r);
  };
  const disarm = () => {
    armed = false;
    markDropTarget(closeBtn, false);
  };
  return {
    // `held` is the row's { meta, isRemote }: a server-only row is never the one open here.
    begin(held) {
      armed = !!held && !held.isRemote && held.meta?.id != null && held.meta.id === app.activeProjectId;
      markDropTarget(closeBtn, armed);
    },
    // True while the armed ✕ is under the pointer, whose drop it then owns.
    track(x, y) {
      if (!armed) return false;
      const hit = over(x, y);
      markDropTarget(closeBtn, true, hit);
      return hit;
    },
    drop(x, y) {
      if (!armed || !over(x, y)) return false;
      disarm();
      confirmCloseProject(app, { ask: false }).then(settle, settle);
      return true;
    },
    end: disarm,
  };
}
