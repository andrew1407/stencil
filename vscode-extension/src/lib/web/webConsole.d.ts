// Shape of webConsole.js — the browser page's console, over VS Code's built-in JS debugger.
export interface DebugConfig { type: string; request: string; name: string; url: string }
export interface EvaluateSession {
  name?: string;
  configuration?: { url?: string };
  customRequest(command: string, args: unknown): Promise<{ result?: string }>;
}
export declare const BROWSERS: Readonly<Record<string, string>>;
export declare const EVAL_TIMEOUT_MS: number;
export declare const PROBE_TIMEOUT_MS: number;
export declare const NO_FACADE: string;
export declare const NO_SESSION: string;
export declare const POLL_MS: number;
export declare const SESSION_NAME: string;
export declare const SESSION_TIMEOUT_MS: number;
export declare const atUrl: (session: EvaluateSession, url: string) => boolean;
export declare const answersFacade: (session: EvaluateSession, opts?: { timeoutMs?: number }) => Promise<boolean>;
export declare const debugConfigFor: (vscode: unknown, url: string) => DebugConfig;
export declare const evaluate: (session: EvaluateSession, expression: string, opts?: { timeoutMs?: number }) => Promise<{ result?: string }>;
export declare const expressionFor: (text: string, opts?: { script?: boolean }) => string;
export declare const loadExpression: (url: string) => string;
export declare const pageSession: (vscode: unknown, url: string, opts?: { timeoutMs?: number }) => Promise<{ session: EvaluateSession | null; reason?: string }>;
