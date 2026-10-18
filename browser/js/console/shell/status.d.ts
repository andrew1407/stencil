// The header line under the logo, as the Zig console prints it.
export interface StatusLine { text: string; name: string; color: string; temporary: boolean; }
export declare const NO_IMAGE_STATUS: string;
export declare const pageLabel: (app: object) => string;
export declare const imageName: (app: object) => string;
export declare const statusLine: (app: object) => StatusLine;
