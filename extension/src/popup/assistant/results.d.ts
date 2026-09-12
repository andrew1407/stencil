// Shapes for popup/assistant/results.js — renders a finished turn: executed-action cards,
// the §11 ask card, and the reply itself.
import type { TurnResult } from '../../llm/chatController.js';

export declare function createResults(opts: {
  view: unknown;
  getItems(): unknown[];
  state: { send(text: string): void };
}): {
  renderResult(result: TurnResult): void;
};
