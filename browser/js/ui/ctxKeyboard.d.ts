export declare const CTX_NAV_KEYS: readonly string[];

/** The row `step` away from `idx` among `count` rows, wrapping; no row yet picks the first/last. */
export declare const ctxKeyStep: (count: number, idx: number, step: number) => number;

/** What Tab walks inside an open flyout: real, visible, enabled form controls in markup order. */
export declare const ctxFocusables: (level: Element) => HTMLElement[];

/** True for a control that keeps Tab for itself (the script flyout's editor indents with it). */
export declare const ctxKeepsTab: (el: Element | null | undefined) => boolean;

export declare const wireCtxKeyboard: (deps: {
  menu: Element;
  menuIsOpen: () => boolean;
  chatRowMenuOpen: () => boolean;
  closeSub: (sub: Element) => void;
  positionSub: (item: Element, sub: Element) => void;
  activeSub: () => Element | null;
  setActiveSub: (sub: Element | null, item: Element | null) => void;
}) => { setKbItem: (item: Element | null) => void };
