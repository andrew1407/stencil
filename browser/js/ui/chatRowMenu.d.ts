export declare const CHAT_POPUP_EVENT: string;
export declare const openComposerMenus: Set<Element>;
export declare const chatRowMenuOpen: () => boolean;
/** True while the row menu or any composer's "…" menu is open (jump pills stand down). */
export declare const chatPopupOpen: () => boolean;
export declare const announcePopup: () => void;

export interface TouchMenuGesture {
  start(key: unknown, x: number, y: number): void;
  move(x: number, y: number): void;
  /** True when this gesture opened the menu — the caller then suppresses the native callout. */
  end(now?: number): boolean;
  cancel(): void;
}

export declare const touchMenuGesture: (
  onOpen: (x: number, y: number) => void,
  opts?: { longPressMs?: number; moveTol?: number; doubleTapMs?: number;
    setTimer?: (fn: () => void, ms: number) => unknown; clearTimer?: (id: unknown) => void },
) => TouchMenuGesture;

/** Right-click, the hover "…" trigger and touch gestures all open the same row menu. */
export declare const wireChatRowMenu: (
  transcript: HTMLElement,
  hooks?: { onInsert?: (text: string) => void; onResend?: (text: string, attachments: unknown[]) => void },
) => void;
