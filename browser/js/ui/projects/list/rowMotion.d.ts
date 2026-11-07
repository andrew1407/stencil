export interface Box { left: number; top: number; width: number; height: number }
export declare const leftHome: (r: DOMRect | Box | null | undefined, view: DOMRect | Box | null | undefined) => Box | null;
export declare function rowLeave(el: HTMLElement | null | undefined, opts?: Record<string, unknown>): Promise<void>;
export declare function rowArrive(el: HTMLElement, opts?: Record<string, unknown>): void;
