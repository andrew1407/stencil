// Shapes for popup/assistant/msgMenu.js — per-message context menu: Copy / Insert / Resend.
import type { Attachment } from '../../llm/chatController.js';

export declare function wireMsgMenu(opts: {
  transcriptEl: HTMLElement;
  inputEl: HTMLTextAreaElement;
  msgMeta: WeakMap<Element, { text: string; resendText?: string; attachments?: Attachment[] }>;
  send: (text: string, attachments?: Attachment[]) => Promise<void>;
  state: { busy: boolean; addMsgMenuBtn: (el: Element) => void };
}): void;
