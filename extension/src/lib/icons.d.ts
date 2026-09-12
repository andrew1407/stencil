export interface IconMap { [name: string]: string; }

export declare const ICONS: IconMap;
export declare function icon(name: string, opts?: { size?: number; cls?: string; sw?: number }): string;
