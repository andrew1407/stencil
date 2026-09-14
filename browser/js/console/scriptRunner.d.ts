// Shape of scriptRunner.js — the .stc op stream mapped onto the window.stencil facade.
import type { Stencil } from './stencilApi.js';
export class ScriptError extends Error {
  readonly line: number;
  readonly col: number;
}
export interface RunScriptOptions {
  fetchLayout?: (src: string, op: unknown) => Promise<unknown>;
}
export const runScript: (
  text: string, stencil: Stencil, options?: RunScriptOptions,
) => Promise<number>;
export const runScriptHere: (text: string, options?: RunScriptOptions) => Promise<number>;
