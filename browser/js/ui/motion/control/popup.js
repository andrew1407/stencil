// A popup's entrance and exit, the dust first: it forms out of (and scatters back into) `point`, or under
// the other modes pours or grows out of the box round that point (genie/window.js menuOpen / menuAway).
import { surfaceIn, surfaceOut } from '../surface/surfaces.js';
import { menuOpen, menuAway, pointBox } from '../genie/window.js';

export const popupIn = (menu, point, ms) => surfaceIn(menu, point, { ms }) || menuOpen(menu, pointBox(point));
export const popupOut = (menu, point, ms) => menuAway(menu, pointBox(point)) || surfaceOut(menu, point, { ms });
