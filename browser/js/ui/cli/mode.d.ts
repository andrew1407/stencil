// CLI mode: the terminal over the page, and the facade gate that comes with it.
export declare const OPEN_CLASS: string;
export declare const cliModeActive: () => boolean;
export declare const enterCliMode: (app: object, opts?: { origin?: { x: number; y: number } | null; doc?: Document }) => boolean;
export declare const exitCliMode: (app: object, doc?: Document) => boolean;
