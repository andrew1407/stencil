// The browser's rows of config/console/commands.json and the word lookup over them.
export interface CommandRow {
  name: string; aliases: string[]; arg: string; help: string; section: string;
  surfaces: string[]; cliMode?: boolean;
}
export interface CliModeTable { typedWord: string; holdMs: number; offCommand: string; }

export declare const COMMANDS: readonly CommandRow[];
export declare const SECTIONS: readonly string[];
export declare const CLI_MODE: Readonly<CliModeTable>;
export declare const commandOf: (word: unknown, opts?: { cliMode?: boolean }) => CommandRow | null;
export declare const completions: (prefix: unknown, opts?: { cliMode?: boolean }) => string[];
