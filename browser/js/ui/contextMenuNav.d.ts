/** The flyout state the menu and the keyboard both work through — one owner, no copies. */
export interface CtxNav {
  closeSub(sub: HTMLElement): void;
  closeAllSubs(): void;
  positionSub(item: HTMLElement, sub: HTMLElement): void;
  repositionActiveSub(): void;
  hideSub(item: HTMLElement, sub: HTMLElement): void;
  /** Closes the WHOLE open chain, not just the deepest flyout. */
  closeActiveSub(): void;
  /** Hover-open + grace-period hide for one submenu parent (a late-built one included). */
  wireSubmenu(item: HTMLElement, sub: HTMLElement): void;
  /** An item with no flyout: hovering it closes any open one. */
  wirePlainItem(item: HTMLElement): void;
  /** Document-level pointer sampling; bound only while the menu is open. */
  setPointerTracking(on: boolean): void;
  /** True when the cursor has not moved since the open flyout was placed. */
  pointerIdle(): boolean;
  activeSub(): HTMLElement | null;
  setActiveSub(sub: HTMLElement | null, item: HTMLElement | null): void;
  /** Hand in ctxKeyboard's highlight setter; the pointer paths clear it through setKbItem. */
  bindKbItem(fn: (item: HTMLElement | null) => void): void;
  setKbItem(item: HTMLElement | null): void;
  /** Seed the pointer with the click point, so idle checks never see a stale position. */
  setLastPointer(x: number, y: number): void;
}

export function createCtxNav(deps: { menu: HTMLElement }): CtxNav;
