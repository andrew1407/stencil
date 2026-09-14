// Shape of pathSearch.js — the executable probe and the memoized PATH walk.
export declare const EXE_SUFFIXES: readonly string[];
export declare const WALK_TTL_MS: number;
export declare const isExecutableFile: (path: string) => boolean;
export declare const onPath: (name: string, env: NodeJS.ProcessEnv) => string | null;
export declare const forgetPathWalk: () => void;
