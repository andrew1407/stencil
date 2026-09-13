export declare const LEAVE_MS: number;
export declare const CHAT_LEAVE_MS: number;
export declare const LEAVING_CLASS: string;

/** Plays `el` out then runs `done` (always, even under reduced motion or with no element). */
export declare function leaveThenRemove(
  el: HTMLElement | null | undefined, done?: () => void, opts?: { ms?: number; cols?: number; rows?: number },
): Promise<void>;

export declare function wipeDurationMs(): number;

export interface ListHold {
  begin(): () => Promise<void>;
  finalizeAll(): void;
  readonly holding: boolean;
}
export declare function createListHold(opts?: {
  settle?: () => void; wait?: () => number; setTimer?: (fn: () => void, ms: number) => unknown;
}): ListHold;

export declare function emptyStateVisible(count: number, holding?: boolean): boolean;

export declare const FILTER_LEAVE_MS: number;
export declare const FILTER_ENTER_MS: number;
export declare const FILTER_OUT_CLASS: string;
export declare const FILTER_IN_CLASS: string;

export declare function diffListKeys(prev?: string[], next?: string[]): { entered: string[]; left: string[] };

export declare function filterLeave(
  el: HTMLElement | null | undefined, done?: () => void,
  opts?: { ms?: number; reduced?: () => boolean; setTimer?: (fn: () => void, ms: number) => unknown },
): Promise<void>;

export interface FilterTransition {
  begin(): string[];
  end(opts?: { skipEnter?: string[] }): { entered: string[]; left: string[] };
  clear(): void;
  readonly ghostCount: number;
}
export declare function createFilterTransition(opts?: {
  list: Element;
  keyAttr?: string;
  ms?: number;
  enterMs?: number;
  reduced?: () => boolean;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
  onLeave?: (el: Element) => void;
}): FilterTransition;

export declare function flashLanding(el: HTMLElement | null | undefined, cls?: string, ms?: number): void;
