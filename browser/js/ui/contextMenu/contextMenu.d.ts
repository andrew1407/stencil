import type { StencilElement } from '../base.js';

export { assistantEnabled, assistantItemHtml } from '../ctx/assistantItem.js';
export { ctxKeyStep, CTX_NAV_KEYS, ctxFocusables } from '../ctx/keyboard.js';

/** The custom right-click context menu. */
export declare class StencilContextMenu extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: object): void;
}
