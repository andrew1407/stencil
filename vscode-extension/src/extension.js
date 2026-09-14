// The activation entry point: wiring only. Each feature registers itself and pushes its own
// disposables onto the context, so deactivate() has nothing left to do.
'use strict';

const commands = require('./commands.js');
const diagnostics = require('./diagnostics.js');
const semanticTokens = require('./semanticTokens.js');

const activate = (context) => {
  diagnostics.register(context);
  semanticTokens.register(context);
  commands.register(context);
};

const deactivate = () => {};

module.exports = { activate, deactivate };
