// The aqua wallpaper painter (config/aqua.json `image`).
/** Paint the whole wallpaper onto a 2D context at its native size. */
export declare const paintAquaImage: (ctx: CanvasRenderingContext2D) => void;
/** The wallpaper encoded as a PNG blob. */
export declare const aquaImageBlob: () => Promise<Blob>;
