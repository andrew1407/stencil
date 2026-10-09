// Which toolbar icons drag, and what each does where it lands (js/ui/drag/): a window icon opens
// its window at the release point, the chat docks or floats, rotate, flip and clear-all act when
// dropped on the canvas, zoom − / + follow the pointer, fit steps through them. No other icon drags.
// Desktop twin: desktop/src/app/drag/ToolbarBuilderDrags.cpp.
import { wireCanvasDrop } from '../../drag/canvasDrop.js';
import { wireChatDrag } from '../../drag/chatDrag.js';
import { wireModalDrags } from '../../drag/modalDrag.js';
import { wireZoomDrag, wireFitDrag } from '../../drag/zoomDrag.js';
import { wireThemeLens } from '../../drag/themeLens.js';

// A drop on the canvas fires these as their click does.
export const CANVAS_CLICK_IDS = Object.freeze(['rotate-left', 'rotate-right', 'flip-horizontal']);

// [id, what its drop on the canvas does]: the eraser clears at once, the drop being its own yes.
export const canvasActs = (app, bar) => [
  ...CANVAS_CLICK_IDS.map((id) => [id, () => bar.$(id)?.click()]),
  ['clear-all-lines', () => app.clearAllLines({ ask: false })],
];

// `bar` is the toolbar element, searched through its scoped `$`; `holds` the zoom buttons'
// setupHoldZoom handles. Returns the ids that took a drag.
export const wireToolbarDrags = (app, bar, holds = {}) => {
  if (typeof bar?.$ !== 'function') return [];
  const wired = [];
  const take = (id, handle) => { if (handle) wired.push(id); };
  const frame = () => app.canvas?.closest?.('.canvas-viewport') ?? null;
  for (const [id, act] of canvasActs(app, bar)) take(id, wireCanvasDrop(bar.$(id), { frame, act }));
  take('chat-btn', wireChatDrag(bar.$('chat-btn'), () => app.chat));
  wired.push(...wireModalDrags(bar));
  const zoomIn = bar.$('zoom-in');
  const zoomOut = bar.$('zoom-out');
  take('zoom-in', wireZoomDrag(zoomIn, +1, app.zoomPan, holds.in));
  take('zoom-out', wireZoomDrag(zoomOut, -1, app.zoomPan, holds.out));
  take('zoom-fit', wireFitDrag(bar.$('zoom-fit'), app.zoomPan, { zoomIn, zoomOut }));
  return wired;
};

// The fullscreen strip's copy of the toolbar (fullscreen/clones.js): a clone carries no listener,
// so its icons are wired again here; each act clicks the copy, which relays to the original.
export const wireCloneDrags = (app, root) => {
  if (typeof root?.querySelector !== 'function') return [];
  const bar = { $: (id) => root.querySelector(`#${id}`) };
  const wired = wireToolbarDrags(app, bar);
  if (wireThemeLens(bar.$('theme-toggle'), app)) wired.push('theme-toggle');
  return wired;
};
