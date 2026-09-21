import type { FloatRect } from './dock.js';

export declare const DOCKS: string[];
export declare const FLOAT_DEFAULT: FloatRect;
export declare const DRAG_THRESHOLD_PX: number;
export declare const DOCK_MIN_SIZE: number;
export declare const DOCK_MAX_FRACTION: number;
export declare const FLOAT_MIN_W: number;
export declare const FLOAT_MIN_H: number;
export declare const COMPACT_CHAT_W: number;
export declare const COMPACT_CHAT_H: number;
export declare const DOCK_ZONE_BAND: number;

export declare const clampFloatRect: (r: Partial<FloatRect> | null | undefined, vw: number, vh: number) => FloatRect;

/** The compact popover shape the toolbar icon's gestures open, anchored to it. */
export declare const compactChatRect: (anchor: Element, vw: number, vh: number) => FloatRect;

/** Resize by dragging edge/corner `dir` ('n'|'s'|'e'|'w'|'ne'|'nw'|'se'|'sw'); the opposite edge stays anchored. */
export declare const resizeFloatRect: (r: FloatRect, dir: string, dx: number, dy: number, vw: number, vh: number) => FloatRect;

/** Which edge drop zone a point falls in during a header drag; corners take the nearest edge. */
export declare const dockZoneAt: (x: number, y: number, vw: number, vh: number, band?: number) => string | null;

export interface GearProbe {
  provider: string;
  url?: string;
  model?: string;
  ok: boolean;
  detail?: string;
}

export interface GearStatusRow { label: string; value: string; state?: string; }

export declare const gearStatusRows: (probe: GearProbe | null | undefined) => GearStatusRow[];
export declare const gearTipFootText: (probe: GearProbe | null | undefined) => string;
