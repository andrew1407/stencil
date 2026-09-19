// Shapes for llm/chatTurn.js — the pieces of one chat turn (contract §7): its limits, the image
// encoders an attachment goes through, the history replay rule, and what a plan says about the
// picture. The controller re-exports the public half of this surface.
import type { ChatImage, ChatMessage } from './llmClient.js';
import type { OpPlan } from './opPlan.js';

export declare const HISTORY_LIMIT: number;
export declare const MAX_IMAGE_EDGE: number;
/** Images ONE message may carry — replayed and paid for per turn (§7). */
export declare const MAX_ATTACHMENTS: number;
/** Appended to the system prompt only on a turn that attached the working image's edge map. */
export declare const EDGE_MAP_SENTENCE: string;
export declare const CHAT_ATTACHMENTS_EVENT: string;
export declare const VIDEO_FRAME_COUNT: number;

/** §2.1 `save` with no name: the attachment's basename, or '' — the surface then falls back. */
export declare const attachmentSaveName: (attachment: { name?: string } | null | undefined) => string;
/** Does the plan WORK ON the picture (an image op, or any variant)? */
export declare const planEditsTheImage: (plan: OpPlan | null | undefined) => boolean;
/** §7 auto-continuation: the plan loaded a picture the model has not seen and drew no layout. */
export declare const planLoadsWithoutTracing: (plan: OpPlan | null | undefined) => boolean;
/** A provider/model that has no vision, answering the auto-attached working image. */
export declare const isImageRejection: (err: unknown) => boolean;

/** data:mediaType;base64,payload → { mediaType, data }, or null. */
export declare const splitDataUrl: (u: unknown) => ChatImage | null;
/** Downscale to ≤ maxEdge px on the long edge and re-encode as a PNG data: URL (§7). */
export declare const downscaleImageToDataUrl: (blob: Blob, maxEdge?: number) => Promise<string>;
/** Re-encode a data URL at thumbnail size; any failure keeps the original URL. */
export declare const thumbnailDataUrl: (dataUrl: string, maxEdge?: number) => Promise<string>;
/** Re-render a snapshot with the core `contour` filter — the edge map the model sees. */
export declare const contourDataUrl: (dataUrl: string) => Promise<string>;
/** §7 replay rule: the current turn keeps its images, the most recent prior image survives, older turns go text-only. */
export declare const replayMessages: (history: ReadonlyArray<ChatMessage>) => ChatMessage[];
