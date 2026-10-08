export interface DockZones {
  /** Lays the four bands on <body>; a second call while shown is a no-op. */
  show(): void;
  /** Lights the band for `side`; null lights none. */
  highlight(side: string | null): void;
  hide(): void;
  readonly shown: boolean;
}

/** The edge drop zones a live chat drag shows. */
export declare function createDockZones(): DockZones;
