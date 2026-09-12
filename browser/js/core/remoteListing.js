// Pure server-listing cache (what the shimmer skeletons answer to) — no DOM here.
// null cache = not loaded, [] = loaded/empty. ensure() starts at most one fetch; a token
// drops a stale in-flight fetch after invalidate(), which must ALSO clear `loading` or
// ensure() never fetches again and the skeletons never resolve.
export const createRemoteListing = (load) => {
  const s = { cache: null, loading: false, failed: false, token: 0 };
  return {
    get cache() { return s.cache; },
    get loading() { return s.loading; },
    get failed() { return s.failed; },
    ensure(done = () => {}) {
      if (s.cache !== null || s.loading) return;
      s.loading = true;
      s.failed = false;
      const myToken = ++s.token;
      load()
        .then((list) => { if (myToken !== s.token) return; s.cache = list || []; s.loading = false; done(); })
        .catch(() => { if (myToken !== s.token) return; s.cache = []; s.failed = true; s.loading = false; done(); });
    },
    invalidate() { s.cache = null; s.failed = false; s.loading = false; s.token++; },
  };
};

// Skeletons show ONLY while a fetch is genuinely in flight; a null cache with no fetch
// running falls through to the honest empty/error state.
export const showsRemoteSkeletons = ({ showServer = false, hasServers = false, cache = null, loading = false } = {}) =>
  !!showServer && !!hasServers && cache === null && !!loading;
