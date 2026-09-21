// Shape of shellQuote.js — the per-shell quoting rules a command line is composed with.
export interface ShellRules {
  safe: RegExp; cd: string; lead: string; quote: (text: string) => string;
}
export declare const DEFAULT_KIND: string;
export declare const SAFE_POSIX: RegExp;
export declare const SAFE_CMD: RegExp;
export declare const SAFE_PS: RegExp;
export declare const SHELLS: Record<string, ShellRules>;
export declare const shellKind: (shell?: string) => string;
export declare const shellFor: (kind?: string) => ShellRules;
