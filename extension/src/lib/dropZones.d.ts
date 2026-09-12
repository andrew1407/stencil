// Self-contained (injected via executeScript({func})) — no module imports at runtime.
export type Quadrant = 'here' | 'incognito' | 'newtab' | 'crop';
export declare const quadrantAt: (x: number, y: number, w: number, h: number) => Quadrant;
export declare const mountDropZones: (accent?: string, editor?: boolean, mode?: 'system' | 'dark' | 'light') => void;
export declare const unmountDropZones: () => void;
