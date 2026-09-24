// One parse per (document, version): a keystroke reaches both the diagnostics and the token
// provider, and the buffer is lexed, parsed and lowered once for the pair.
import { loadParser } from './parserHost.js';
import { LIMIT, versionCache } from './spawn/versionCache.js';

const programs = versionCache();

const programFor = async (document) => {
  const { parseScript } = await loadParser();
  return programs.get(document, (buffer) => parseScript(buffer.getText()));
};

const forget = (uri) => programs.forget(uri);

export { LIMIT, forget, programFor };
