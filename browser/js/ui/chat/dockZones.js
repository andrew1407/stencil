// The four edge bands a chat drag can dock into, up only while the drag is live: the panel's
// header drag and the toolbar icon's (ui/drag/chatDrag.js) show the same ones. Transient DOM on
// <body>; components/chat/dockZones.css draws them.
import { icon } from '../icons.js';
import { DOCK_SIDES } from './geometry.js';

const ARROW = { left: 'left', right: 'right', top: 'up', bottom: 'down' };

export function createDockZones() {
  let zonesEl = null;
  const show = () => {
    if (zonesEl) return;
    zonesEl = document.createElement('div');
    zonesEl.className = 'chat-dock-zones';
    for (const side of DOCK_SIDES) {
      const z = document.createElement('div');
      z.className = `chat-dock-zone chat-dock-zone-${side}`;
      z.dataset.side = side;
      const arrow = document.createElement('span');
      arrow.className = 'chat-zone-arrow';
      arrow.innerHTML = icon(`chevron-${ARROW[side]}`, { size: 18 });
      z.appendChild(arrow);
      zonesEl.appendChild(z);
    }
    document.body.appendChild(zonesEl);
  };
  // `side` null lights none.
  const highlight = (side) => {
    if (!zonesEl) return;
    for (const z of zonesEl.children) z.classList.toggle('chat-dock-zone-active', z.dataset.side === side);
  };
  const hide = () => { zonesEl?.remove(); zonesEl = null; };
  return { show, highlight, hide, get shown() { return !!zonesEl; } };
}
