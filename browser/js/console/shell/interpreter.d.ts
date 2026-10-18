// One console line against the facade, shared by the console window and the CLI stage.
import type { HelpLine } from './help.js';
import type { CommandHandler } from './handlers.js';

export type PrintKind = 'echo' | 'plain' | 'error' | 'help' | 'secret' | 'title';

/** What a host lends the interpreter: output, the file picker, and whether CLI mode is on. */
export interface ConsoleHost {
  print: (lines: (string | HelpLine)[], kind: PrintKind) => void;
  clear: () => void;
  close: () => void;
  /** A bare `/upload`: the browser's own file chooser. */
  pickFile: () => void;
  cliMode: () => boolean;
  /** A slashed word in CLI mode: true when it was a skin word and was applied. */
  secret: (word: string) => boolean;
  /** Whether a word is a secret, for the gold echo. */
  isSecret?: (word: string) => boolean;
  /** `/theme` takes a worn skin off first, as the Zig console does. */
  skinOff?: () => void;
  /** The picture changed: the status line repaints. */
  statusChanged?: () => void;
}

export interface Interpreter {
  run: (line: string) => Promise<void>;
  /** The line Tab makes of `line`: one match filled in, several extended to their common prefix; null = unchanged (an ambiguous one lists them). */
  complete: (line: string) => string | null;
  /** The colour the line being typed wears: 'cmd' (a known command), 'secret' (a skin word), or ''. */
  classify: (line: string) => 'cmd' | 'secret' | '';
  history: { up: () => string; down: () => string; atEnd: () => boolean; readonly entries: string[] };
  handlerNames: () => string[];
}

export declare const createInterpreter: (opts: {
  app: object; host: ConsoleHost; extra?: Record<string, CommandHandler>; stencil?: object;
}) => Interpreter;
