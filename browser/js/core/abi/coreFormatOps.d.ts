// The text-formatting part of the wasm op map: the console logo banner rows.
import type { CoreOps } from './stencilCore.js';

export type FormatOpName = 'logoBanner';

export declare const buildFormatOps: (core: unknown) => Pick<CoreOps, FormatOpName>;
