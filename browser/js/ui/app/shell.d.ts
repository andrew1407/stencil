// The editor shell DrawingApp stands on: its fixed nodes and the theme <html> carries.

export declare const shellNodes: () => { canvas: HTMLCanvasElement; tooltip: HTMLElement; coordinatesBody: HTMLElement };
export declare const htmlTheme: () => 'dark' | 'light';
/** The data-accent attribute as set, or null. */
export declare const htmlAccent: () => string | null;
/** The inline --accent override, '' when a named preset is active. */
export declare const htmlCustomAccent: () => string;
