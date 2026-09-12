export interface CtxAssistantChatHost {
  sending(): boolean;
  setSending(v: boolean): void;
  menuIsOpen(): boolean;
  closeMenu(): void;
  positionSub(item: Element, sub: Element): void;
  setActiveSub(sub: Element | null, item: Element | null): void;
  bumpBusy(): void;
  setOnMenuClose(fn: () => void): void;
}

/** Wires the Assistant flyout's chat (the same shared conversation as the panel). */
export declare const wireCtxAssistantChat: (
  app: object, host: CtxAssistantChatHost, openChatPanel: () => void,
) => void;
