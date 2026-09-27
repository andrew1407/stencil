// The scalar half of the wasm op map: colour, distance, formulas, durations, the zoom clamp,
// its bounds and the zoom-to-rect fit, and the close-shape test. Each name is the JS reference
// implementation it replaces.
import type { CoreOps } from './stencilCore.js';
import type { CoreMarshal } from './coreMarshal.js';

export type ScalarOpName =
  'parseHex' | 'distToSegment' | 'formulaValidate' | 'formulaApply' |
  'formulaValidateCtx' | 'formulaApplyCtx' |
  'parseDuration' | 'clampScale' | 'zoomMin' | 'zoomMax' | 'rectZoom' | 'shouldCloseShape';

export declare const buildScalarOps: (core: unknown, m: CoreMarshal) => Pick<CoreOps, ScalarOpName>;
