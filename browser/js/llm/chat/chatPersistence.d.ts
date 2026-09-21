// Opt-in per-project chat persistence (llm-contract.md §12): glues the shared conversation
// to the IndexedDB chat store and, for server-linked projects, the server's `chat` file
// kind. OFF until the user opts in; temporary/incognito editors NEVER persist. Changes
// snapshot synchronously, keyed by the active project id at that instant.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { ChatStore } from './chatStore.js';
import type { LlmSettings } from '../llmSettings.js';

export interface ChatPersistence {
  /** Swap the conversation to project `id` (null = a fresh temporary/blank editor). */
  projectOpened(id: string | null): Promise<void>;
  /** Stored chats go with their projects regardless of the toggle. */
  projectRemoved(id: string): Promise<void>;
  allProjectsCleared(): Promise<void>;
  /** Test seam: resolves when every snapshot filed so far has been written. */
  flush(): Promise<void>;
}

export interface ChatPersistenceOptions {
  app: DrawingApp;
  store?: ChatStore;
  getSettings?: () => Pick<LlmSettings, 'saveChats'>;
  now?: () => number;
}

export declare const createChatPersistence: (opts: ChatPersistenceOptions) => ChatPersistence;
/** Entry-point wiring: lands on `app.chatPersistence` and restores the boot project's chat. */
export declare const wireChatPersistence: (app: DrawingApp, opts?: Omit<ChatPersistenceOptions, 'app'>) => ChatPersistence;
