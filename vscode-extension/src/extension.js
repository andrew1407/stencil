// The activation entry point: wiring only. Each feature registers itself and pushes its own
// disposables onto the context, so deactivate() has nothing left to do.
'use strict';

const FEATURES = [
  require('./diagnostics.js'), require('./semanticTokens.js'), require('./completion.js'),
  require('./hover.js'), require('./colors.js'), require('./commands.js'),
];

const activate = (context) => { for (const feature of FEATURES) feature.register(context); };

const deactivate = () => {};

module.exports = { activate, deactivate };
