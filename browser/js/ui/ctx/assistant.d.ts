export interface CtxAssistantHost {
  menu: Element;
  wireSubmenu(item: Element, sub: Element): void;
  menuIsOpen(): boolean;
  closeAllSubs(): void;
}

/** Gates and wires the context menu's Assistant entry; call once per menu instance. */
export declare const wireCtxAssistant: (
  app: object, host: CtxAssistantHost,
) => { syncAssistant: () => void };
