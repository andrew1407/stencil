// Shape of emitTargets.js — what `--script-emit` can write, and where it lands.
export declare const EMIT_TARGETS: readonly { label: string; description: string }[];
export declare function emitTarget(path: string, extension: string): string;
export declare function pickEmitTarget(vscode: unknown): Promise<string | null>;
