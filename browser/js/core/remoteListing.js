// Pure server-listing cache lifted out of ui/projectsModal.js — no DOM here.

// ── Server-listing cache (what the shimmer skeletons answer to) ─────────────
// null cache = not loaded, [] = loaded/empty. ensure() starts at most one fetch; a token
// drops a stale in-flight fetch after invalidate(), which must ALSO clear `loading` or
// ensure() never fetches again and the skeletons never resolve. Pure factory — unit-tested.
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

// Skeleton rows may show ONLY while a server-listing fetch is genuinely in flight; a null
// cache with no fetch running must fall through to the honest empty/error state, or the
// skeletons stay up forever. Pure — unit-tested.
export const showsRemoteSkeletons = ({ showServer = false, hasServers = false, cache = null, loading = false } = {}) =>
  !!showServer && !!hasServers && cache === null && !!loading;
