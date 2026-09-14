// Shapes for popup/assistant/turnRunner.js — one turn start to finish: drains the tray,
// posts the user's message, runs the controller behind the stop button, renders the result.
import type { Attachment, TurnResult } from '../../llm/chatController.js';

export interface TurnRunner {
  setBusy(busy: boolean): void;
  /** `preset` is an answer submitted by a §11 ask card rather than typed. */
  send(preset?: string, requeue?: Attachment[]): Promise<void>;
}

export declare function createTurnRunner(opts: {
  sendBtn: HTMLButtonElement;
  inputEl: HTMLTextAreaElement;
  view: unknown;
  tray: { pending: Attachment[]; renderTray(): void; syncClearBtn(): void };
  renderResult(result: TurnResult): void;
  state: {
    busy: boolean; turnAbort: AbortController | null; llmSettings: unknown;
    controller: { send(text: string, opts: { attachments: Attachment[]; signal?: AbortSignal }): Promise<TurnResult> };
    wipeAfterTurn: boolean;
  };
}): TurnRunner;
