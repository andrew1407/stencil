// Shapes for llm/chatRespond.js — one model round of a chat turn (contract §7): the wire copy's
// edge map, the text-only retry, plan execution, the auto-continuation round and the §10 deferred
// flush. The controller owns the state this is handed; nothing here is persisted.
import type { Stencil } from '../console/stencilApi.js';
import type { ChatImage, ChatMessage, LlmClient } from './llmClient.js';
import type { Attachment, ChatCapabilities, TurnResult } from './chatController.js';
import type { SavedServerEntry } from './opPlan.js';

/** The controller's mutable turn state, shared with the send loop. */
export interface TurnState {
  /** THIS turn's user images, kept past the queue drain so an editing plan can adopt one. */
  turnAttachments: Array<{ dataUrl: string; name: string }>;
  videoInput: Attachment | null;
  /** Latched once a provider refuses images: text-only edits must still be plannable. */
  textOnlyModel: boolean;
}

export interface ResponderOptions {
  stencil?: Stencil;
  state: TurnState;
  history: ChatMessage[];
  pushHistory: (msg: ChatMessage) => void;
  /** The §7 edge map of a just-taken snapshot; null when there is none. */
  edgeOf: (snapshotUrl: string) => Promise<ChatImage | null>;
  snapshot: (() => Promise<string>) | null;
  getClient: () => LlmClient;
  loadImage: (dataUrl: string, name?: string) => Promise<void>;
  frameAt?: (file: File, index: number) => Promise<string>;
  exportImage?: () => Promise<string>;
  previewThumb: (dataUrl: string) => Promise<string>;
  savedServers?: () => SavedServerEntry[];
  openIncognito?: (url: string) => Promise<void>;
  /** The §10 capability bag every op execution replays. */
  caps: ChatCapabilities;
}

/** One round against the current history; it recurses once for the §7 auto-continuation. */
export declare const createResponder: (opts: ResponderOptions) => (round: {
  signal?: AbortSignal;
  preWarnings: string[];
  autoAttached: boolean;
  continued: boolean;
  edgeImage?: ChatImage | null;
  deferred?: Array<{ op: string }>;
}) => Promise<TurnResult>;
