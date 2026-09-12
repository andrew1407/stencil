export declare const DISPATCH_WINDOW_MS: number;

export interface OnceGate {
  allow(): boolean;
  suppressed(): boolean;
  reset(): void;
}

export declare function createOnceGate(opts?: { windowMs?: number; now?: () => number }): OnceGate;
