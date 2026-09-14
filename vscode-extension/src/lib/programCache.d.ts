// Shape of programCache.js — one parse of a buffer, shared by the two features that read it.
import type { ScriptProgram } from '../parser/index.js';
export declare const LIMIT: number;
export declare const programFor: (document: unknown) => Promise<ScriptProgram>;
export declare const forget: (uri: unknown) => void;
