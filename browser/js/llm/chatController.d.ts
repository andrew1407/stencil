// Chat controller: history, attachments, and the send loop. Owns the client-side
// conversation state (contract §7): history is replayed in full on every call (bounded to
// HISTORY_LIMIT), images follow the replay rule, attachments are downscaled before
// base64-encoding. Every DOM capability is INJECTED for `node --test`.
import type { Stencil } from '../console/stencilApi.js';
import type { ChatImage, ChatMessage, LlmClient } from './llmClient.js';
import type { AskPreview, OpPlan, PlanAsk, SavedServerEntry, VariantResult } from './opPlan.js';

/** A queued attachment. Videos are sampled into `frames`; the bytes never reach the model. */
export interface Attachment {
  name: string;
  kind: 'image' | 'video';
  /** 'working' loads it into the editor on send; 'analyze' only attaches it to the turn. */
  use: 'analyze' | 'working';
  dataUrl?: string;
  frames?: string[];
  file?: File;
}

/** What one turn produced. Typed LlmErrors and invalid-plan errors propagate instead. */
export interface TurnResult {
  reply: string;
  warnings: string[];
  results: VariantResult[];
  ask: PlanAsk | null;
  /** The §11 option previews, thumbnail-sized. */
  askPreviews: AskPreview[];
  chatOnly: boolean;
}

/** A note string when nothing happened (unknown / ambiguous / declined), null when the work was done. */
export type CapabilityNote = string | null;

/** The §10 / §2.1 capabilities the surface injects (llm/adapters/*); an absent one makes its op note+skip or fail. */
export interface ChatCapabilities {
  exportImage?: () => Promise<string>;
  /** The pool §10 `connect` resolves in — the user's saved servers, never a model-named host. */
  savedServers?: () => SavedServerEntry[];
  openIncognito?: (url: string) => Promise<void>;
  saveProject?: (name?: string | null) => Promise<string>;
  copyRendered?: () => Promise<unknown>;
  copyLayoutRendered?: () => Promise<unknown>;
  removeProjectNamed?: (name: string) => Promise<CapabilityNote>;
  clearWorkingImage?: () => Promise<CapabilityNote>;
  clearLocalProjects?: (keepCurrent: boolean) => Promise<CapabilityNote>;
  renameActiveProject?: (name: string) => Promise<CapabilityNote>;
  setBlankColor?: (color: string) => Promise<CapabilityNote>;
  openProjectNamed?: (name: string, last: boolean) => Promise<CapabilityNote>;
  clearChatConversation?: () => Promise<CapabilityNote>;
  setChatPlacement?: (placement: { open?: boolean | null; dock?: string | null }) => Promise<CapabilityNote>;
  /** null closes whatever dialog is open. */
  openDialog?: (name: string | null) => Promise<CapabilityNote>;
  setVoiceChat?: (on: boolean) => CapabilityNote;
}

export interface ChatControllerOptions extends ChatCapabilities {
  stencil?: Stencil;
  /** Re-read per send, so settings changes take effect. */
  getClient: () => LlmClient;
  prepareAttachment?: (file: Blob) => Promise<string>;
  extractFrames?: (file: File, count: number) => Promise<string[]>;
  frameAt?: (file: File, index: number) => Promise<string>;
  loadImage?: (dataUrl: string, name?: string) => Promise<void>;
  previewThumb?: (dataUrl: string) => Promise<string>;
  /** The §7 auto-attach; defaults to exportImage downscaled to MAX_IMAGE_EDGE. */
  workingSnapshot?: () => Promise<string>;
  /** Renders a snapshot with the core contour filter (§7 edge map); best-effort. */
  edgeMap?: (dataUrl: string) => Promise<string>;
  onAttachmentsChanged?: () => void;
}

export interface ChatController {
  /** [{ role, text, images? }] — the canonical wire shape, bounded to HISTORY_LIMIT. */
  history: ChatMessage[];
  /** Pending for the next send. */
  attachments: Attachment[];
  /** The current working-video attachment (frame ops valid), or null. */
  readonly videoInput: Attachment | null;
  /** Images are downscaled now, videos sampled into frames; throws past MAX_ATTACHMENTS. */
  addAttachment(file: File): Promise<Attachment>;
  addImageDataUrl(dataUrl: string, name?: string): Attachment;
  removeAttachment(i: number): void;
  /** History, queued attachments and the working-video binding go; settings and image stay. */
  clearConversation(): void;
  /** Replace the conversation with a RESTORED one (§12): text-only turns. */
  seedHistory(messages: ReadonlyArray<{ role?: string; text?: string }> | null | undefined): void;
  setAttachmentUse(i: number, use: Attachment['use']): void;
  /** Retry: re-queue the drained analyze-attachments into an EMPTY queue. */
  requeueLastTurnAttachments(): void;
  /** One turn: replay → client.chat → parseOpPlan → executeOpPlan (+ the §7 continuation). */
  send(text: string, opts?: { signal?: AbortSignal }): Promise<TurnResult>;
}

export declare const HISTORY_LIMIT: number;
export declare const MAX_IMAGE_EDGE: number;
/** Images ONE message may carry — replayed and paid for per turn (§7). */
export declare const MAX_ATTACHMENTS: number;
/** Appended to the system prompt only on a turn that attached the working image's edge map. */
export declare const EDGE_MAP_SENTENCE: string;
export declare const CHAT_ATTACHMENTS_EVENT: string;
export declare const VIDEO_FRAME_COUNT: number;
/** Does the plan WORK ON the picture (an image op, or any variant)? */
export declare const planEditsTheImage: (plan: OpPlan | null | undefined) => boolean;
/** data:mediaType;base64,payload → { mediaType, data }, or null. */
export declare const splitDataUrl: (u: unknown) => ChatImage | null;
/** Downscale to ≤ maxEdge px on the long edge and re-encode as a PNG data: URL (§7). */
export declare const downscaleImageToDataUrl: (blob: Blob, maxEdge?: number) => Promise<string>;
/** Re-render a snapshot with the core `contour` filter — the edge map the model sees. */
export declare const contourDataUrl: (dataUrl: string) => Promise<string>;
/** §7 replay rule: the current turn keeps its images, the most recent prior image survives, older turns go text-only. */
export declare const replayMessages: (history: ReadonlyArray<ChatMessage>) => ChatMessage[];
/** §7 auto-continuation: the plan loaded a picture the model has not seen and drew no layout. */
export declare const planLoadsWithoutTracing: (plan: OpPlan | null | undefined) => boolean;
export declare const createChatController: (opts: ChatControllerOptions) => ChatController;
