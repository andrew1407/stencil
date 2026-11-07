// The picture and the word an empty editor receives when a skin turns on.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { CodecLine } from '../../core/line/linesCodec.js';

export interface SkinScene {
  /** The file name the picture loads under; its base name names the project. */
  imageName: string;
  /** The local project the skin reopens instead of minting another. */
  projectName: string;
  image: { width: number; height: number };
  /** The picture, encoded. */
  blob: () => Promise<Blob>;
  /** The word as closed, locked, filled lines across a w×h picture. */
  wordLines: (w: number, h: number) => CodecLine[];
}
/** Leave incognito, load the picture under its name, install the word; true once the word is down. */
export declare const createSkinScene: (app: DrawingApp, scene: SkinScene) => Promise<boolean>;
/** Reopen the local (not server-linked) project named `projectName`; false when there is none. */
export declare const openSkinProject: (app: DrawingApp, projectName: string) => boolean;
/** Reopen the skin's project or create its scene, and wait for the picture either way. */
export declare const settleSkinScene: (app: DrawingApp, scene: SkinScene) => Promise<void>;
/** `paint` run over a canvas of `image`'s size, encoded as a PNG blob; rejects naming `name`. */
export declare const paintedBlob: (image: { width: number; height: number },
  paint: (ctx: CanvasRenderingContext2D) => void, name: string) => Promise<Blob>;
