// Lazy import of ../parser/, memoized — never a rejected one, which would poison every later parse.
// The copies' UTF-8 column helper imports nothing and weighs nothing, so it passes straight through.
export { columnIndex } from '../parser/script/types.js';

let pending = null;

const loadParser = (importer = () => import('../parser/index.js')) => {
  pending ??= importer().catch((error) => { pending = null; throw error; });
  return pending;
};

export { loadParser };
