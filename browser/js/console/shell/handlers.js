// The console's dispatch table: one handler per command name of config/console/commands.json,
// the edit and session halves merged, plus whatever a host adds (the CLI stage's own words).
import { editHandlers } from './handlersEdit.js';
import { sessionHandlers } from './handlersSession.js';

export const createHandlers = ({ app, stencil, extra = {} }) =>
  Object.freeze({ ...editHandlers({ app, stencil }), ...sessionHandlers({ app, stencil }), ...extra });

export const handlerNames = (handlers) => Object.keys(handlers);
