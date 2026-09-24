export interface PixelOpts { accent?: string | null; dark?: boolean }
export declare const pixelRects: (rows: string[], ink?: Record<string, string> | null) => string;
export declare const hasPixelIcon: (name: string) => boolean;
export declare const pixelArt: (name: string, opts?: PixelOpts) => string;
export declare const pixelIconSvg: (name: string, opts?: PixelOpts, size?: number) => string;
export declare const swapIcons: (root: Element | null | undefined, on: boolean, opts?: PixelOpts) => void;
