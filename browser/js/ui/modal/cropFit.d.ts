/** The crop window's size: fitted around its picture on open, the picture re-fitted on every resize. */
export declare const VIEW_MARGIN: number;
export declare const STAGE_MIN: number;

export type Size = { width: number; height: number };

/** The largest iw:ih picture inside roomW × roomH; `scale` is display px per image px (0: none). */
export declare function fitContain(iw: number, ih: number, roomW: number, roomH: number): Size & { scale: number };

/** The most a picture opens at for this viewport (desktop previewFitBox). */
export declare function previewBox(viewport: Size): Size;

/** The window's opening size around `chrome`, the window less its picture's room. */
export declare function cropWindowSize(args: {
  iw: number;
  ih: number;
  chrome: Size;
  viewport: Size;
  floorW?: number;
}): Size;

/**
 * Sizes the window on `fitWindow(prepare)` (`prepare` runs laid out, before the measurements); the
 * stage then follows its free room, reporting each scale. `onViewport` answers a window resize: a held
 * size stays while it fits, else the window re-fits.
 */
export declare function wireCropFit(
  els: { overlay: HTMLElement; box: HTMLElement; frame: HTMLElement; stage: HTMLElement; footer: HTMLElement },
  dims: () => { iw: number; ih: number },
  onScale: (scale: number) => void,
): { fitWindow: (prepare?: () => void) => boolean; fitStage: () => boolean; onViewport: () => void };
