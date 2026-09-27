// One parse per (document, version): a keystroke reaches both the diagnostics and the token
// provider, and the buffer is lexed, parsed and lowered once for the pair. The program carries
// the `lines` it was parsed from, so its byte spans convert against the text they describe.
import { loadParser } from './parserHost.js';
import { sourceLines } from './spans.js';
import { LIMIT, versionCache } from './spawn/versionCache.js';

const programs = versionCache();

const programFor = async (document) => {
  const { parseScript } = await loadParser();
  return programs.get(document, (buffer) => {
    const text = buffer.getText();
    return Object.assign(parseScript(text), { lines: sourceLines(text) });
  });
};

const forget = (uri) => programs.forget(uri);

export { LIMIT, forget, programFor };
