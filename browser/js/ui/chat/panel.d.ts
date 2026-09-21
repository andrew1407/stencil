import type { StencilElement } from '../base.js';

export {
  FLOAT_MIN_W, FLOAT_MIN_H, clampFloatRect, COMPACT_CHAT_W, COMPACT_CHAT_H, compactChatRect,
  resizeFloatRect, DOCK_ZONE_BAND, dockZoneAt, gearStatusRows, gearTipFootText,
} from './geometry.js';

/** The assistant chat panel; plans execute only through window.stencil, never edit pixels directly. */
export declare class StencilChatPanel extends StencilElement {
  static inner(): string;
  static template(): string;
}
