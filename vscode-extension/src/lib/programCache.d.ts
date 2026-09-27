// Shape of programCache.js — one parse of a buffer, shared by the two features that read it.
import type { ScriptProgram } from '../parser/index.js';
export declare const LIMIT: number;
/** The program plus the buffer lines it was parsed from (its spans are UTF-8 byte columns). */
export declare const programFor: (document: unknown) => Promise<ScriptProgram & { lines: string[] }>;
export declare const forget: (uri: unknown) => void;
