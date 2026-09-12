// Shapes for popup/assistant/attachments.js — the composer's pending-attachment tray.
import type { Attachment } from '../../llm/chatController.js';

export interface AttachmentsController {
  /** Live array — callers push/splice it directly (e.g. a chip's × removal). */
  pending: Attachment[];
  renderTray(): void;
  addPending(p: Attachment): void;
  addPendingUrl(url: string): Promise<void>;
  addPendingFile(file: File): Promise<void>;
  syncClearBtn(): void;
}

export declare function createAttachments(opts: {
  trayEl: HTMLElement;
  transcriptEl: HTMLElement;
  clearBtn: HTMLButtonElement;
  getItems(): unknown[];
  getPageUrl(): string;
  addWarn(text: string): void;
  attachImage(index: number, entry: unknown): Promise<Attachment['image']>;
  isBusy(): boolean;
}): AttachmentsController;
