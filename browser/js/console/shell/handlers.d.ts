// The console's dispatch table over the facade.
import type { HelpLine } from './help.js';
import type { ConsoleHost } from './interpreter.js';

export interface HandlerDeps { app: object; stencil: object; }
/** What a command prints: nothing, one line, several, or help lines; it throws the error to show. */
export type HandlerOutput = void | string | string[] | HelpLine[];
export type CommandHandler = (arg: string, host: ConsoleHost) => HandlerOutput | Promise<HandlerOutput>;
export type CommandHandlers = Readonly<Record<string, CommandHandler>>;

export declare const createHandlers: (deps: HandlerDeps & { extra?: Record<string, CommandHandler> }) => CommandHandlers;
export declare const handlerNames: (handlers: CommandHandlers) => string[];
