// Lazy import of ../parser/, memoized — never a rejected one, which would poison every later parse.
// The copies' UTF-8 column helpers import nothing and weigh nothing, so they pass straight through.
export { unitIndexOfColumn, utf8Length } from '../parser/script/types.js';

let pending = null;

const loadParser = (importer = () => import('../parser/index.js')) => {
  pending ??= importer().catch((error) => { pending = null; throw error; });
  return pending;
};

export { loadParser };
