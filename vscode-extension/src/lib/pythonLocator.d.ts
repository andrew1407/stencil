// Shape of pythonLocator.js — the interpreter a .pystc runs on.
export declare const MISSING_PYTHON_MESSAGE: string;
export declare const NAMES: readonly string[];
export declare function locatePython(opts?: {
  configured?: string; baseDir?: string; env?: Record<string, string | undefined>;
}): string | null;
export declare function pythonFor(vscode: unknown, document: unknown): string | null;
