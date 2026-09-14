// The bridge from this CommonJS extension to the ESM parser copies in ../parser/. One
// dynamic import, memoized — never a rejected one, which would poison every later parse.
'use strict';

let pending = null;

const loadParser = (importer = () => import('../parser/index.js')) => {
  pending ??= importer().catch((error) => { pending = null; throw error; });
  return pending;
};

module.exports = { loadParser };
