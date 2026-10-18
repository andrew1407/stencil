// The CLI stage's own words and the secret skin words.
import type { CommandHandler } from '../../console/shell/handlers.js';
export interface CliStageWords { leave: () => void; clear: () => void; printSecret: (lines: string[]) => void; wearSkin: (name: string | null) => void; skin: () => string | null; }
export declare const cliCommands: (deps: { app: object; stage: CliStageWords }) => Record<string, CommandHandler>;
export declare const secretWord: (word: unknown, stage: CliStageWords) => boolean;
