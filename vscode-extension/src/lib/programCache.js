// One parse per (document, version): a keystroke reaches both the diagnostics and the token
// provider, and the buffer is lexed, parsed and lowered once for the pair.
'use strict';

const { loadParser } = require('./parserHost.js');
const { LIMIT, versionCache } = require('./versionCache.js');

const programs = versionCache();

const programFor = async (document) => {
  const { parseScript } = await loadParser();
  return programs.get(document, (buffer) => parseScript(buffer.getText()));
};

const forget = (uri) => programs.forget(uri);

module.exports = { LIMIT, forget, programFor };
