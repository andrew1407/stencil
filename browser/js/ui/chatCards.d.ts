import type { Attachment } from '../llm/chatController.js';
import type { AskPreview, PlanAsk, VariantResult } from '../llm/opPlan.js';

export declare const chatAttachmentStrip: (attachments: readonly Attachment[]) => HTMLElement;

export declare const chatResultCard: (r: VariantResult) => HTMLElement;

/** The model's §11 `ask`; submitting sends the answer as the user's next turn. */
export declare const chatAskCard: (
  ask: PlanAsk, opts?: { onSubmit?: (answer: string) => void; previews?: readonly AskPreview[] },
) => HTMLElement;

export declare const chatReconnectButton: (serverUrl: string, onReconnect?: (url: string) => void) => HTMLElement;

export declare const chatConfigureButton: (onBeforeOpen?: () => void) => HTMLElement;
