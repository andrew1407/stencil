// Shape of pathSearch.js — the executable probe and the memoized PATH walk.
// Node's own `NodeJS.ProcessEnv` needs @types/node, which this tree does not depend on.
export type ProcessEnv = Record<string, string | undefined>;
export declare const EXE_SUFFIXES: readonly string[];
export declare const WALK_TTL_MS: number;
export declare const isExecutableFile: (path: string) => boolean;
export declare const onPath: (name: string, env: ProcessEnv) => string | null;
export declare const forgetPathWalk: () => void;
