// Chat panel geometry + gear tip text: pure, no DOM; the panel wires window.inner*.
import { clamp } from '../../utils/math.js';
import { popoverPosition } from '../tip/popover.js';
import { PROVIDER_LABELS } from '../../llm/client.js';
export const DOCKS = Object.freeze(['left', 'right', 'top', 'bottom', 'float']);

export const FLOAT_DEFAULT = Object.freeze({ x: 80, y: 80, w: 360, h: 440 });
export const DRAG_THRESHOLD_PX = 4;      // plain header clicks must not twitch the panel
export const DOCK_MIN_SIZE = 240;
export const DOCK_MAX_FRACTION = 0.8;    // docked panel never exceeds 80% of the viewport

export const FLOAT_MIN_W = 280;
export const FLOAT_MIN_H = 220;
export const clampFloatRect = (r, vw, vh) => {
  const w = clamp(Math.round(r?.w || FLOAT_DEFAULT.w), FLOAT_MIN_W, vw);
  const h = clamp(Math.round(r?.h || FLOAT_DEFAULT.h), FLOAT_MIN_H, vh);
  return {
    x: clamp(Math.round(r?.x || 0), 0, vw - w),
    y: clamp(Math.round(r?.y || 0), 0, vh - h),
    w,
    h,
  };
};

// The compact shape the toolbar icon's popover gestures open: floated small next to
// the icon, sized like the context-menu chat flyout.
export const COMPACT_CHAT_W = 340;
export const COMPACT_CHAT_H = 460;
export const compactChatRect = (anchor, vw, vh) => {
  const w = Math.min(COMPACT_CHAT_W, vw);
  const h = Math.min(COMPACT_CHAT_H, vh);
  const p = popoverPosition({ anchor, box: { width: w, height: h }, viewport: { width: vw, height: vh } });
  return clampFloatRect({ x: p.left, y: p.top, w, h }, vw, vh);
};

// Resize by dragging edge/corner `dir` (n|s|e|w|ne|nw|se|sw); the opposite edge stays anchored.
export const resizeFloatRect = (r, dir, dx, dy, vw, vh) => {
  let { x, y, w, h } = r;
  const right = x + w, bottom = y + h;
  if (dir.includes('e')) w = Math.min(Math.max(FLOAT_MIN_W, w + dx), vw - x);
  if (dir.includes('s')) h = Math.min(Math.max(FLOAT_MIN_H, h + dy), vh - y);
  if (dir.includes('w')) { w = Math.min(Math.max(FLOAT_MIN_W, w - dx), right); x = right - w; }
  if (dir.includes('n')) { h = Math.min(Math.max(FLOAT_MIN_H, h - dy), bottom); y = bottom - h; }
  return { x: Math.round(x), y: Math.round(y), w: Math.round(w), h: Math.round(h) };
};

// Which edge drop zone a point falls in during a header drag; corners take the nearest edge.
export const DOCK_ZONE_BAND = 72;
export const dockZoneAt = (x, y, vw, vh, band = DOCK_ZONE_BAND) => {
  const dist = { left: x, right: vw - x, top: y, bottom: vh - y };
  let best = null;
  for (const side of ['left', 'right', 'top', 'bottom']) {
    if (dist[side] <= band && (best == null || dist[side] < dist[best])) best = side;
  }
  return best;
};

// The gear's status tooltip is a table (.chat-status-tip); `state` colours the Status cell.
export const gearStatusRows = (probe) => {
  if (!probe) return [{ label: 'Status', value: 'Checking the configured LLM…', state: 'connecting' }];
  if (probe.provider === 'none') {
    return [
      { label: 'Provider', value: PROVIDER_LABELS.none },
      { label: 'Status', value: 'Assistant turned off — nothing is sent anywhere', state: 'error' },
    ];
  }
  const rows = [{ label: 'Provider', value: PROVIDER_LABELS[probe.provider] || probe.provider || '—' }];
  if (probe.url) rows.push({ label: 'Endpoint', value: probe.url.replace(/^https?:\/\//i, '') });
  rows.push({ label: 'Model', value: probe.model || 'server default' });
  rows.push(probe.ok
    ? { label: 'Status', value: `Connected${probe.detail ? ` — ${probe.detail}` : ''}`, state: 'ok' }
    : { label: 'Status', value: probe.detail || 'Unreachable', state: 'error' });
  return rows;
};

// The tooltip's footer lines, always ending with the click hint.
export const gearTipFootText = (probe) => {
  const lines = [];
  if (!probe || probe.provider === 'none' || probe.ok) { /* the table says it all */ }
  else {
    lines.push(`No LLM reachable${probe.url ? ` at ${probe.url}` : ''} — ${probe.provider === 'ollama' ? 'start Ollama or ' : ''}configure another provider.`);
  }
  lines.push('Click to configure the assistant');
  return lines.join('\n');
};
