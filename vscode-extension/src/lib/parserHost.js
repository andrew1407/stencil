// The bridge from this CommonJS extension to the ESM parser copies in ../parser/. One
// dynamic import, memoized: the module graph is loaded on the first .stc file and shared by
// diagnostics and semantic tokens for the life of the extension host.
'use strict';

let pending = null;

const loadParser = () => {
  if (!pending) pending = import('../parser/index.js');
  return pending;
};

module.exports = { loadParser };
