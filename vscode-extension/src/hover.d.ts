// Shape of hover.js — the Markdown explanation of the token under the caret.
import type { ScriptToken } from './parser/scriptTypes.js';
export declare const PARAM_DOC: string;
export declare const tokenAt: (
  tokens: ScriptToken[], position: { line: number; character: number },
) => ScriptToken | undefined;
export declare const markdownAt: (
  tokens: ScriptToken[], position: { line: number; character: number },
) => string;
export declare const provider: {
  provideHover: (document: unknown, position: unknown) => Promise<unknown>;
};
export declare const register: (context: unknown) => unknown;
