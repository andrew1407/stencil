// The server-listing cache the shimmer skeletons answer to: null = not loaded, [] =
// loaded/empty. ensure() starts at most one fetch; invalidate() drops a stale in-flight
// one by token AND clears `loading`, or the skeletons never resolve. Pure factory.

export interface RemoteListing<T = unknown> {
  readonly cache: T[] | null;
  readonly loading: boolean;
  readonly failed: boolean;
  ensure(done?: () => void): void;
  invalidate(): void;
}
export declare const createRemoteListing: <T = unknown>(load: () => Promise<T[] | null | undefined>) => RemoteListing<T>;

/** Skeleton rows show ONLY while a listing fetch is genuinely in flight. */
export declare const showsRemoteSkeletons: (state?: {
  showServer?: boolean; hasServers?: boolean; cache?: unknown[] | null; loading?: boolean;
}) => boolean;
