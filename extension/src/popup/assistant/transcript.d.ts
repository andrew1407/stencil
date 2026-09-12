// Shapes for popup/assistant/transcript.js — every entry the panel can put on screen
// (messages, warnings, cards, the attachment strip, the typing placeholder, the empty-
// state chips) and the clear that scatters them all away.
import type { Attachment } from '../../llm/chatController.js';

export interface TranscriptView {
  clearConversation(): void;
  scrollDown(): void;
  appendEntry(node: Element, opts?: { arrive?: boolean; wrap?: boolean }): Element;
  appendDiv(className: string, text: string, opts?: { arrive?: boolean }): Element;
  dismissible(el: Element, autoMs?: number): Element;
  /** What the per-message context menu acts on: the message's text (+ its attachments). */
  msgMeta: WeakMap<Element, { text: string; resendText?: string; attachments?: Attachment[] }>;
  addMsg(cls: string, text: string): Element;
  addWarn(text: string): Element;
  addAttachments(list: Attachment[]): Element;
  addRetry(el: Element, text: string, send: (text: string, attachments?: Attachment[]) => unknown, attachments?: Attachment[]): void;
  addConfigureCta(el: Element): void;
  addThinking(): Element;
  addCard(iconName: string, label: string, ok?: boolean, autoMs?: number): Element;
  hideSuggestions(): void;
  showSuggestions(): void;
}

export declare function createTranscript(opts: {
  sectionEl: HTMLElement;
  transcriptEl: HTMLElement;
  inputEl: HTMLTextAreaElement;
  tray: { pending: Attachment[]; renderTray(): void; syncClearBtn(): void };
  state: { busy: boolean; controller: { clearConversation(): void }; workingScan: unknown; addMsgMenuBtn(el: Element): void };
}): TranscriptView;
