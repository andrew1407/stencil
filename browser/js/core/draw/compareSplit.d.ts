// A split compare view's halves: the original over the original side, and the divider bar +
// knob at a constant on-screen size (config/constants.json COMPARE_DIVIDER over the zoom).

type SplitApp = {
  canvas: { width: number; height: number };
  image: CanvasImageSource;
  compareSplit?: number;
  scale?: number;
};
type Ctx2D = CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;

/** The divider's position as a fraction of the width (vertical) or height, clamped to 0…1. */
export declare const splitFraction: (app: { compareSplit?: number }) => number;
export declare const paintOriginalSide: (ctx: Ctx2D, app: SplitApp, mode: 'vertical' | 'horizontal') => void;
/** Erase the original side from the lines layer, once the lines are down. */
export declare const eraseOriginalSide: (ctx: Ctx2D, app: SplitApp, mode: 'vertical' | 'horizontal') => void;
export declare const paintDivider: (ctx: Ctx2D, app: SplitApp, mode: 'vertical' | 'horizontal') => void;
