// Shape of scriptValues.js — length tokens, colours and point lists.
import type { ScriptDiagnostic, ScriptToken } from './scriptTypes.js';
export interface ArgCursor { args: ScriptToken[]; i: number }
export const cursorOf: (args: ScriptToken[]) => ArgCursor;
export const joinWords: (args: ScriptToken[]) => string;
export const isPunct: (t: ScriptToken, text: string) => boolean;
export const skipPunct: (c: ArgCursor, text: string) => void;
export const isColorToken: (t: ScriptToken) => boolean;
export const applyUnit: (number: string, unit: string, fallback: string) => string;
export const readLengthRaw: (c: ArgCursor) => { number: string; unit: string } | null;
export const readLength: (c: ArgCursor, defaultUnit: string) => string | null;
export const readPointList: (
  c: ArgCursor, defaultUnit: string, diags: ScriptDiagnostic[],
) => string[] | null;
