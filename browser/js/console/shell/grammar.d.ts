// The console line grammar: an optional leading '/', the first word, the rest.
export interface ParsedLine { word: string; arg: string; slashed: boolean; }
export interface ConnectArg { url: string; token: string | null; }
export interface CropSpecArg { x1?: string; x2?: string; y1?: string; y2?: string; aspect?: string; album?: boolean; }

export declare const slashed: (line: unknown) => boolean;
export declare const parseCommandLine: (line: unknown) => ParsedLine;
export declare const splitWords: (arg: unknown) => string[];
export declare const looksLikeServerUrl: (word: string) => boolean;
export declare const parseConnectArgs: (arg: unknown) => ConnectArg[];
/** null when a token is not `edge=value` or `album`. */
export declare const parseCropSpec: (arg: unknown) => CropSpecArg | null;
