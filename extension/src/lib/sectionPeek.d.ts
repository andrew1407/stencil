export declare const PEEK_CLOSE_GRACE_MS: number;
export declare function isTypingTarget(t: EventTarget | null | undefined): boolean;

export interface Rect { left: number; top: number; bottom: number; }
export interface Box { width: number; height: number; }
export interface Viewport { width: number; height: number; }
export declare function peekPosition(
  args: { anchor: Rect; box: Box; viewport: Viewport; gap?: number; margin?: number },
): { left: number; top: number };

export interface SectionPeek {
  enterHead(section: unknown, altHeld: boolean): void;
  altPressed(section: unknown): void;
  enterPeek(): void;
  leave(): void;
  altReleased(): void;
  dismiss(): void;
  sectionToggled(section: unknown): void;
  isOpen(): boolean;
  openSection(): unknown;
}

export declare function createSectionPeek(opts?: {
  isCollapsed: (section: unknown) => boolean;
  open: (section: unknown) => void;
  close: (section: unknown) => void;
  isEngaged?: (section: unknown) => boolean;
  grace?: number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}): SectionPeek;
