// Cross-tab message vocabulary shared by the SharedWorker coordinator, the window-side
// TabsCoordinator and the BroadcastChannel fallback. Frozen: the string keys live here only.

export declare const MSG: Readonly<{
  HELLO: 'hello';
  HERE: 'here';
  ACTIVE: 'active';
  ACCENT: 'accent';
  PROJECTS_CHANGED: 'projects-changed';
  BYE: 'bye';
  TABCOUNT: 'tabcount';
  PEERS: 'peers';
  INCOGNITO: 'incognito';
  INCOGNITOS: 'incognitos';
}>;

/** What happened to the project set, carried on a PROJECTS_CHANGED message. */
export declare const PROJECT_ACTION: Readonly<{
  UPDATED: 'updated';
  REMOVED: 'removed';
  CLEARED: 'cleared';
  CLOSE: 'close';
}>;
