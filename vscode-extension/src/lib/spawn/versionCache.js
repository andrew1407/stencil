// One answer per (document, version): a keystroke asks the same question of several providers,
// and no answer outlives its edit. A document with no version is never cached.
'use strict';

const LIMIT = 8;

const keyFor = (document) => (typeof document?.version === 'number'
  ? `${document.uri} ${document.version}`
  : null);

const versionCache = ({ limit = LIMIT } = {}) => {
  const entries = new Map();

  const get = (document, compute) => {
    const key = keyFor(document);
    if (key === null) return compute(document);
    if (!entries.has(key)) {
      entries.set(key, compute(document));
      for (const stale of [...entries.keys()].slice(0, -limit)) entries.delete(stale);
    }
    return entries.get(key);
  };

  const forget = (uri) => {
    const prefix = `${uri} `;
    for (const key of entries.keys()) if (key.startsWith(prefix)) entries.delete(key);
  };

  return { forget, get };
};

module.exports = { LIMIT, keyFor, versionCache };
