export interface GifLook { fps?: number; width?: number; colors?: number; scale?: string }

export function webmToGif(src: string, dst: string, opts?: GifLook & { start?: number; end?: number }): string;
export function framesToGif(dir: string, dst: string, opts?: GifLook & { inFps?: number; pattern?: string }): string;
export function quantizePng(file: string): string;
