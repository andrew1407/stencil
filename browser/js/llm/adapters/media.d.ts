// §10 media adapters: the picture, its frames, and the clipboard — the slice of the chat
// controller's capability bag that reads or writes the working image, each a thin call
// onto the same app methods the toolbar uses.
import type { DrawingApp } from '../../core/drawingApp.js';

export interface MediaAdapters {
  /** The working image as a PNG data: URL; throws (with words) on an empty editor. */
  exportImage(): Promise<string>;
  /** Downscale an attachment to the §7 edge limit and re-encode it as a PNG data: URL. */
  prepareAttachment(file: Blob): Promise<string>;
  /** `n` evenly spaced frames of a video as data: URLs. */
  extractFrames(file: File, n: number): Promise<string[]>;
  frameAt(file: File, index: number): Promise<string>;
  /** Adopt incognito IN PLACE and load `url` there, so the rest of the plan runs on it. */
  openIncognito(url: string): Promise<void>;
  /** The clipboard write's real outcome: resolves on success, rejects on a blocked write. */
  copyRendered(): Promise<void>;
  copyLayoutRendered(): Promise<void>;
}

export declare const mediaAdapters: (app: DrawingApp) => MediaAdapters;
