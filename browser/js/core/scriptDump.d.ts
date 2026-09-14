// Shape of scriptDump.js — the canonical text form the fixtures compare against.
import type { ScriptProgram } from './script.js';
export const dumpProgram: (program: ScriptProgram) => string;
export const dumpDiagnostics: (program: ScriptProgram) => string;
