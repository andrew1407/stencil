// Shape of tokenClassify.js — one legend type per parser token, decided from its statement.
import type { ScriptToken } from '../parser/scriptTypes.js';
export declare const GROUP_TYPE: Record<string, string>;
export declare const KIND_TYPE: Record<string, string>;
export declare const classify: (tokens: ScriptToken[]) => (string | undefined)[];
