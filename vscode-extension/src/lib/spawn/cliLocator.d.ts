// Shape of cliLocator.js — finding the Stencil CLI without consulting a shell.
import type { ProcessEnv } from '../pathSearch.js';
export interface LocateOptions { configured?: string; env?: ProcessEnv }
export declare const MISSING_CLI_MESSAGE: string;
export declare const RELATIVE_CLI_MESSAGE: string;
export declare const isExecutableFile: (path: string) => boolean;
/** True for a non-empty path that is not absolute: such a value is refused. */
export declare const isRelative: (configured?: string) => boolean;
/** The absolute executable `configured` names, else null (relative values included). */
export declare const resolveConfigured: (configured: string) => string | null;
export declare const onPath: (name: string, env: ProcessEnv) => string | null;
export declare const locateCli: (options?: LocateOptions) => string | null;
export declare const cliFor: (vscode: unknown, document?: unknown) => string | null;
/** Why cliFor found nothing: the relative-path refusal, else "not found". */
export declare const missingCliMessage: (vscode: unknown, env?: ProcessEnv) => string;
