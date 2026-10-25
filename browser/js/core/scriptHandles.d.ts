// Shape of scriptHandles.js — the wasm side of the .stc engine.
import type { ScriptProgram } from './script.js';
import type { StencilCore } from './abi/stencilCore.js';

export const buildScriptOps: (
  core: StencilCore,
  marshal: { withCString: <T>(text: string, use: (ptr: number, len: number) => T) => T },
) => { scriptParse: (text: string) => ScriptProgram };
