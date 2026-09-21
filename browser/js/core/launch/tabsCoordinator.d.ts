// Window-side cross-tab coordination: the SharedWorker coordinator when available, else a
// BroadcastChannel roll-call, else single-tab assumptions. Never touches localStorage —
// it relays small control messages so the projects UI knows tab count and peers.

export interface TabCount { count: number; youAreOnly: boolean; }

/** This tab's incognito session as peers list it. */
export interface IncognitoSession { name: string; updatedAt: number; }

/** What rode a PROJECTS_CHANGED message (worker/messages.js PROJECT_ACTION). */
export interface ProjectsChangedDetail { id?: string | null; action?: string; }

export declare class TabsCoordinator {
  constructor();
  onTabCount(cb: (count: TabCount) => void): () => void;
  onPeers(cb: (activeIds: (string | null)[]) => void): () => void;
  onProjectsChanged(cb: (detail: ProjectsChangedDetail) => void): () => void;
  onAccent(cb: (key: string) => void): () => void;
  onIncognitoPeers(cb: (sessions: IncognitoSession[]) => void): () => void;
  /** Resolves with the first tab count, or after READY_TIMEOUT_MS with the last known one. */
  whenReady(): Promise<TabCount>;
  reportActive(id: string | null | undefined): void;
  reportIncognito(session: IncognitoSession | null): void;
  broadcastAccent(key: string): void;
  projectsChanged(detail?: ProjectsChangedDetail): void;
}
