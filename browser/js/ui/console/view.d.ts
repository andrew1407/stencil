// The console's scrollback, text only, swept in by the reveal plan.
import type { HelpLine } from '../../console/shell/help.js';
import type { PrintKind } from '../../console/shell/interpreter.js';
export interface ConsoleView {
  print: (lines: (string | HelpLine)[], kind?: PrintKind) => void;
  clear: () => void;
  lines: () => string[];
  revealing: () => boolean;
  /** Lands every line still typing in whole, at once. */
  settleNow: () => void;
  onSettled: (fn: () => void) => void;
  /** How an accent run (a help head, an echo's prompt and word, a section) is filled; null is plain text. */
  setDecorator: (fn: ((el: HTMLElement, shown: string, lineText: string) => void) | null) => void;
  /** Every accent run in the scrollback. */
  runs: () => HTMLElement[];
}
export declare const createConsoleView: (pre: HTMLElement, opts?: {
  reveal?: { plan: (now: number) => { jumps: number; stepMs: number }; reset: () => void };
  now?: () => number; schedule?: (fn: () => void, ms: number) => unknown;
}) => ConsoleView;
