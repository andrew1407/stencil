// Shape of typingsFile.js — the facade's types as a file a workspace can hold.
export declare const JS_CONFIG: string;
export declare const JS_CONFIGS: readonly string[];
export declare const PACKAGED: string;
export declare const TYPINGS_FILE: string;
export declare const hasJsConfig: (folder: string) => boolean;
export declare const install: (folder: string) => { typings: string; config: string };
export declare const installedIn: (folder: string) => boolean;
export declare const typingsPath: (folder: string) => string;
