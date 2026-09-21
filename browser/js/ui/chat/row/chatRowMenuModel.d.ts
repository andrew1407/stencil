import type { AskPreview, PlanAsk, VariantResult } from '../../../llm/plan/opPlan.js';
import type { Attachment } from '../../../llm/chat/chatController.js';

/** One transcript row, as chatSession.js's shared log stores it. */
export interface ChatRow {
  id: string | number;
  role: 'user' | 'assistant' | 'error';
  text: string;
  pending?: boolean;
  error?: boolean;
  retryText?: string | null;
  card?: boolean;
  reconnect?: string | null;
  attachments?: Attachment[];
  results?: VariantResult[];
  ask?: PlanAsk | null;
  askPreviews?: AskPreview[];
}

export interface ChatRowMenuItem { id: 'copy' | 'insert' | 'resend'; label: string; icon: string; }

export declare const chatRowMenuItems: (row: ChatRow | null | undefined) => ChatRowMenuItem[];

/** The hover "…" trigger on a bubble corner; null when the row has no menu items. */
export declare const chatRowMenuButton: (row: ChatRow) => HTMLButtonElement | null;

export declare const CHAT_ROW_MENU_JUMP_GAP: number;

/** How far `btn` must rise to clear every jump pill it overlaps; 0 when none touch it. */
export declare const rowMenuLiftPx: (btn: DOMRect | null | undefined, pills?: readonly DOMRect[], gap?: number) => number;

export declare const rowMenuLiftFits: (row: DOMRect | null | undefined, btn: DOMRect | null | undefined, lift: number) => boolean;

export declare const copyChatText: (text: string, doc?: Document, nav?: Navigator | null) => Promise<boolean>;

export declare const selectionCoversRow: (rowEl: Element, win?: Window | null) => boolean;
