// One parse per (document, version). A keystroke reaches both the diagnostics and the token
// provider, and each used to lex, parse and lower the buffer for itself. A document carrying
// no version number is never cached, because nothing would ever invalidate it.
'use strict';

const { loadParser } = require('./parserHost.js');

const LIMIT = 8;
const programs = new Map();

const keyFor = (document) => (typeof document.version === 'number'
  ? `${document.uri} ${document.version}`
  : null);

const remember = (key, program) => {
  programs.set(key, program);
  for (const stale of [...programs.keys()].slice(0, -LIMIT)) programs.delete(stale);
  return program;
};

const programFor = async (document) => {
  const { parseScript } = await loadParser();
  const key = keyFor(document);
  if (key === null) return parseScript(document.getText());
  return programs.get(key) ?? remember(key, parseScript(document.getText()));
};

// Every version of one document, on close.
const forget = (uri) => {
  const prefix = `${uri} `;
  for (const key of programs.keys()) if (key.startsWith(prefix)) programs.delete(key);
};

module.exports = { LIMIT, forget, programFor };
