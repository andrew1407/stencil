import type { ChatController } from '../../llm/chatController.js';

export declare const CHAT_ATTACHMENTS_EVENT: string;
export declare const notifyAttachmentsChanged: () => void;

/** Patches `container`'s attachment chips to match `controller.attachments` in place. */
export declare const chatAttachmentChips: (
  container: HTMLElement, controller: Pick<ChatController, 'attachments' | 'removeAttachment'> | null | undefined,
) => void;
