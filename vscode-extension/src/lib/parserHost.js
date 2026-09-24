// Lazy import of ../parser/, memoized — never a rejected one, which would poison every later parse.

let pending = null;

const loadParser = (importer = () => import('../parser/index.js')) => {
  pending ??= importer().catch((error) => { pending = null; throw error; });
  return pending;
};

export { loadParser };
