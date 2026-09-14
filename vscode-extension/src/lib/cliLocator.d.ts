// Shape of cliLocator.js — finding the Stencil CLI without consulting a shell.
export interface LocateOptions { configured?: string; baseDir?: string; env?: NodeJS.ProcessEnv }
export declare const MISSING_CLI_MESSAGE: string;
export declare const isExecutableFile: (path: string) => boolean;
export declare const resolveConfigured: (configured: string, baseDir: string) => string | null;
export declare const onPath: (name: string, env: NodeJS.ProcessEnv) => string | null;
export declare const locateCli: (options?: LocateOptions) => string | null;
export declare const cliFor: (vscode: unknown, document: unknown) => string | null;
