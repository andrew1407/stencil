// Shape of terminal.js — the one place a shell command line is composed.
export interface TerminalRun { cli: string; args: string[]; cwd?: string }
export declare const TERMINAL_NAME: string;
export declare const SAFE: RegExp;
export declare const quoteArg: (value: unknown) => string;
export declare const commandLine: (cli: string, args?: string[]) => string;
export declare const reuseTerminal: (vscode: unknown, cwd?: string) => unknown;
export declare const runInTerminal: (vscode: unknown, run: TerminalRun) => unknown;
