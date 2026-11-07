export interface Probe { provider: string; url?: string; model?: string; ok?: boolean; detail?: string; }
export interface StatusRow { label: string; value: string; state?: 'connecting' | 'ok' | 'error'; }
export declare const gearStatusRows: (probe: Probe | null, labels?: Record<string, string>) => StatusRow[];
export declare const gearTipFootText: (probe: Probe | null) => string;

export interface ChatStatusTip {
  el: HTMLElement;
  show(): void;
  hide(): void;
  setProbe(p: Probe | null): void;
  wire(btn: HTMLElement): void;
  /** While `fn` answers true the tip neither shows nor stays: the menu its trigger opened is up. */
  setBlocked(fn: () => boolean): void;
}
export declare const createChatStatusTip: (opts?: { doc?: Document; getAnchor?: () => HTMLElement | null;
  labels?: Record<string, string>; win?: Window }) => ChatStatusTip;
