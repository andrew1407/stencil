// The skins' pictures and the cat's walk.
export interface SkinFrame { frame: number; seed: number; }
export declare const pictureAt: (name: string, rowKey: number, col: number, prev: number | null, clock: SkinFrame) => number;
export declare const picture: (name: string, i: number) => string;
export declare const catPose: (width: number, rows: number, clock: SkinFrame) => { col: number; row: number; text: string; cols: number };
