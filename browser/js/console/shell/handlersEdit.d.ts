// The edit half of the console's commands, each a facade call.
import type { CommandHandlers, HandlerDeps } from './handlers.js';
export interface BlankArgs { format: string | null; width: number | null; height: number | null; color: string; }
/** null when the tokens are present but malformed. */
export declare const parseBlank: (arg: unknown) => BlankArgs | null;
export declare const editHandlers: (deps: HandlerDeps) => CommandHandlers;
