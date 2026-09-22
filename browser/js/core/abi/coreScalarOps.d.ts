// The scalar half of the wasm op map: colour, distance, formulas, durations, the zoom clamp
// and the close-shape test. Each name is the JS reference implementation it replaces.
import type { CoreOps } from './stencilCore.js';
import type { CoreMarshal } from './coreMarshal.js';

export type ScalarOpName =
  'parseHex' | 'distToSegment' | 'formulaValidate' | 'formulaApply' |
  'formulaValidateCtx' | 'formulaApplyCtx' |
  'parseDuration' | 'clampScale' | 'shouldCloseShape';

export declare const buildScalarOps: (core: unknown, m: CoreMarshal) => Pick<CoreOps, ScalarOpName>;
