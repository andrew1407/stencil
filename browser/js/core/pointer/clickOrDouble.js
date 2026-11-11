// One control, two meanings: a click runs `single` once the double-click window passes, a second
// click inside it runs `double` instead. A keyboard press (detail 0) is a click at once. No DOM:
// the handler reads only the event it is given (desktop twin: support/share/menuDoubleClick.hpp).
import { DOUBLE_CLICK_MS, DOUBLE_TAP_MS } from '../project/openGesture.js';

export const clickOrDouble = ({ single, double }) => {
  let timer = null;
  return (e) => {
    if (timer) { clearTimeout(timer); timer = null; double(e); return; }
    if (!e || e.detail === 0) { single(e); return; }
    timer = setTimeout(() => { timer = null; single(e); }, e.pointerType === 'touch' ? DOUBLE_TAP_MS : DOUBLE_CLICK_MS);
  };
};
