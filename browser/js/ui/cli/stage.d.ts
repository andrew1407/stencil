// The CLI stage: the Zig console over the whole page, a view over the live editor.
import type { Interpreter } from '../../console/shell/interpreter.js';
import type { ConsoleView } from '../console/view.js';
export declare const STAGE_CLASS: string;
export interface CliStage {
  host: HTMLElement; view: ConsoleView; interpreter: Interpreter;
  skin: () => string | null; wearSkin: (name: string | null) => void; release: () => void;
}
export declare const openCliStage: (app: object, opts?: { doc?: Document; onLeave?: () => void;
  schedule?: (fn: () => void, ms: number) => unknown }) => CliStage;
