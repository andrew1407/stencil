// Shapes for llm/openActions.js — translates validated open.actions (contract §8)
// onto the existing `#stencil=` editor launch options.
import type { PlanAction } from './op/plan.js';

export interface OpenLaunch {
  crop?: { x: number; y: number; w: number; h: number };
  layout?: {
    lines: unknown[];
    imageFilter?: string;
    filterColor?: string;
    imageWidth?: number;
    imageHeight?: number;
  };
  page?: { size: string };
}

/** Pure. `rotate` has no launch slot and is dropped with a warning. */
export declare function translateOpenActions(
  actions: PlanAction[] | null | undefined,
  dims?: { width?: number; height?: number },
): { launch: OpenLaunch; warnings: string[] };
