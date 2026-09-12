// Minimal named-channel pub/sub behind the in-memory subscription registries
// (TabsCoordinator channels, ServerConnection's events feed). DOM-free on purpose.

export declare class Emitter {
  /** Subscribe; returns the unsubscribe function. */
  on(name: string, cb: (...args: unknown[]) => void): () => void;
  /** A throwing subscriber is isolated so the rest still fire. */
  emit(name: string, ...args: unknown[]): void;
  clear(): void;
}
