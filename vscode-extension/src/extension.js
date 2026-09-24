// The activation entry point: wiring only. Each feature registers itself and pushes its own
// disposables onto the context, so deactivate() has nothing left to do.
import * as colors from './colors.js';
import * as commands from './commands.js';
import * as completion from './completion.js';
import * as decorations from './decorations.js';
import * as diagnostics from './diagnostics.js';
import * as hover from './hover.js';
import * as jsHints from './jsHints.js';
import * as semanticTokens from './semanticTokens.js';
import * as typings from './typings.js';
import * as webCommands from './webCommands.js';
const FEATURES = [diagnostics, semanticTokens, completion, hover, decorations, colors, commands, jsHints, webCommands, typings];
export const activate = (context) => { for (const feature of FEATURES) feature.register(context); };
export const deactivate = () => {};
