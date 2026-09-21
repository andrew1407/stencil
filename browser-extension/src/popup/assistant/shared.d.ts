// Shapes for popup/assistant/shared.js — the transcript's arrive/leave gestures and the
// two small entry adapters used across popup/assistant/*.
import type { ChatImage } from '../../llm/client.js';

/** Scatters `el` out (a whole-transcript clear coarsens the mesh via `count`/`index`). */
export declare const chatLeave: (el: Element, done: () => void, count?: number, index?: number) => unknown;
/** Dusts `el` in against `host`, the section it arrives inside. Returns `el`. */
export declare const chatEnter: (el: Element, host: Element) => Element;
/** Downscale + re-encode any image source as PNG → the LlmImage {mediaType,data}. */
export declare const toLlmImage: (source: { dataUrl?: string; blob?: Blob }) => Promise<ChatImage>;
export declare const entryName: (entry: { name?: string } | null | undefined) => string;
