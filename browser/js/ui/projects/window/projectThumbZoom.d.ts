export interface ProjectThumbZoom {
  /** Wire hover-magnify preview on one project-row thumbnail element. */
  enableThumbZoom(thumbEl: Element): void;
  hideZoom(): void;
}

/** Magnified hover preview for project-row thumbnails; one reused floating element. */
export declare const createThumbZoom: () => ProjectThumbZoom;
