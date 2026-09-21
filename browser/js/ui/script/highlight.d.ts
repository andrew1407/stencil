import type { ScriptProgram } from '../../core/script.js';

/** Repaints `pre` as coloured spans of `text`; diagnostics are underlined only when asked. */
export declare const paintInto: (
  pre: Element, text: string, withDiagnostics: boolean,
) => ScriptProgram;

/** Writes the first error (else warning) of `program` into the diagnostics strip. */
export declare const showDiagnostic: (strip: Element, program: ScriptProgram | null) => void;
