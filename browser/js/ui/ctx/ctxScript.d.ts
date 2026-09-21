import type { CtxScriptEditorHost } from './ctxScriptEditor.js';

export interface CtxScriptHost extends CtxScriptEditorHost {
  wireSubmenu(item: Element, sub: Element): void;
  menuIsOpen(): boolean;
  closeAllSubs(): void;
}

/** Gates and wires the context menu's Stencil Script entry; call once per menu instance. */
export declare const wireCtxScript: (
  app: object, host: CtxScriptHost,
) => { syncScript: () => void; dropEditor: () => void };
