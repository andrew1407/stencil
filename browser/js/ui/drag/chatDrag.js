// The chat icon dragged off the toolbar: the dock zones the panel's own header drag shows come up;
// released in one the chat opens docked on that side (an open chat moves there), anywhere else
// away from the icon it floats, its top-left corner on the release point and formed out of the
// cursor. Desktop twin: installChatDrag in desktop/src/app/drag/toolbarDrags.{hpp,cpp}.
import { wireIconDrag, dropAnchor } from './iconDrag.js';
import { createDockZones } from '../chat/dockZones.js';
import { dockZoneAt } from '../chat/geometry.js';
import { PHONE_MEDIA } from '../../utils.js';

// Where a release puts the chat: a dock side, else the client point its float's top-left corner takes.
export const chatSpotAt = (x, y, vw, vh) => dockZoneAt(x, y, vw, vh) ?? { x, y };

const viewSize = () => ({ w: window.innerWidth, h: window.innerHeight });
// A phone-width page shows the panel as a centred modal: there is nowhere to dock it.
const phoneShape = () => typeof matchMedia === 'function' && matchMedia(PHONE_MEDIA).matches;

// `chat()` is the panel's surface (app.chat), which wires after the toolbar.
export const chatDragHooks = ({ chat, zones = createDockZones(), view = viewSize, phone = phoneShape }) => {
  const zoneAt = (p) => {
    const v = view();
    return p.overOrigin ? null : dockZoneAt(p.x, p.y, v.w, v.h);
  };
  return {
    start: () => {
      if (typeof chat()?.openAt !== 'function' || phone()) return false;
      zones.show();
      return true;
    },
    move: (p) => zones.highlight(zoneAt(p)),
    drop: (p) => {
      zones.hide();
      const v = view();
      chat().openAt(chatSpotAt(p.x, p.y, v.w, v.h), { from: dropAnchor(p.x, p.y) });
    },
    cancel: () => zones.hide(),
  };
};

export const wireChatDrag = (btn, chat) => (btn ? wireIconDrag(btn, chatDragHooks({ chat })) : null);
