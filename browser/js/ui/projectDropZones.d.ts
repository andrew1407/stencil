export interface ProjectDropZones {
  showZones(): void;
  hideZones(): void;
  /** null over the dialog card; else 'here' | 'newtab' | 'remove'. */
  zoneForPoint(x: number, y: number): 'here' | 'newtab' | 'remove' | null;
  highlightZone(zone: string | null): void;
}

/** The projects list's drag-out drop zones overlay, painted lazily over `overlay`. */
export declare const createDropZones: (overlay: Element) => ProjectDropZones;
