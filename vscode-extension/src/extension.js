// The activation entry point: wiring only. Each feature registers itself and pushes its own
// disposables onto the context, so deactivate() has nothing left to do.
'use strict';

const commands = require('./commands.js');
const completion = require('./completion.js');
const diagnostics = require('./diagnostics.js');
const hover = require('./hover.js');
const semanticTokens = require('./semanticTokens.js');

const FEATURES = [diagnostics, semanticTokens, completion, hover, commands];

const activate = (context) => { for (const feature of FEATURES) feature.register(context); };

const deactivate = () => {};

module.exports = { activate, deactivate };
