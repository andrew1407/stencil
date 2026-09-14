export interface CtxScriptEditorHost {
  closeMenu(): void;
  positionSub(item: Element, sub: Element): void;
  setActiveSub(sub: Element | null, item: Element | null): void;
  setSending(v: boolean): void;
  bumpBusy(): void;
}

/**
 * Wires the Stencil Script flyout's editor (the script window's behaviour, compacted).
 * Returns the call that drops it off the shared script buffer.
 */
export declare const wireCtxScriptEditor: (
  app: object, host: CtxScriptEditorHost, openScriptWindow: () => void,
) => () => void;
