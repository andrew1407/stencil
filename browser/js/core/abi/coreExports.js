// Which wasm exports the app calls at runtime and which only tests/wasm drives, so a stale
// artifact missing a parity-only wrapper loses that op alone instead of the whole core.
import { stateExports } from './coreHandles.js';

export const RUNTIME_EXPORTS = Object.freeze([
  'stencil_parseHex', 'stencil_formulaValidate', 'stencil_formulaApply',
  'stencil_formulaValidateCtx', 'stencil_formulaApplyCtx', 'stencil_formulaApplyPoints', 'stencil_parseDuration',
  'stencil_clampScale', 'stencil_zoomMin', 'stencil_zoomMax', 'stencil_shouldCloseShape',
  'stencil_isAlbumOrientation',
  'stencil_cropAspect', 'stencil_cropResizeScale', 'stencil_pageDimensions',
  'stencil_rotatePoints', 'stencil_flipPoints', 'stencil_boundingBoxCenter',
  'stencil_applyFilterRGBA', 'stencil_applyContourRGBA', 'stencil_centeredCrop',
  'stencil_resizeCropFromCorner', 'stencil_moveCropClamped', 'stencil_scaleCropCentered',
  'stencil_swapCropOrientation', 'stencil_cropChange', 'stencil_rotateCropRectQuarter',
  'stencil_snapCropRect', 'stencil_rotateEditQuarter', 'stencil_mirrorEdit', 'stencil_mergeLinesKeep',
  'stencil_logoBanner',
  // core/wasmScriptApi.cpp's, which scriptHandles.js wraps.
  'stencil_scriptParse', 'stencil_scriptDestroy', 'stencil_scriptErrorCount',
  'stencil_scriptDiagCount', 'stencil_scriptDiagAt', 'stencil_scriptTokenCount',
  'stencil_scriptBlockCount', 'stencil_scriptBlockAt',
  'stencil_scriptOpCount', 'stencil_scriptOpAt', 'stencil_scriptOpStr',
  'stencil_scriptOpNum', 'stencil_scriptOpResolve', 'stencil_scriptDump',
  'stencil_scriptOpTokCount', 'stencil_scriptOpTok', 'stencil_scriptDiagRelated',
  'stencil_scriptTokens',
]);

// Op names → the exports behind them; `state` is the handle classes and project rules together.
export const PARITY_EXPORTS = Object.freeze({
  distToSegment: ['stencil_distToSegment'],
  rectZoom: ['stencil_rectZoom'],
  pageFormats: ['stencil_pageFormats'],
  pixelToPageRaw: ['stencil_pixelToPageRaw'],
  unchainLine: ['stencil_chainUnchain'],
  pullOutPoint: ['stencil_chainPullOut'],
  state: stateExports,
});

export const missingExports = (mod, syms) => syms.filter((sym) => typeof mod[`_${sym}`] !== 'function');

export const droppedParityOps = (mod) =>
  Object.keys(PARITY_EXPORTS).filter((op) => missingExports(mod, PARITY_EXPORTS[op]).length > 0);
