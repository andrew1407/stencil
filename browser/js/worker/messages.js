// ── Cross-tab message types ─────────────────────────────────────
// Shared protocol vocabulary for the SharedWorker coordinator and the
// window-side TabsCoordinator (and the BroadcastChannel fallback). Frozen so
// the string keys live in exactly one place instead of as scattered literals.
export const MSG = Object.freeze({
  HELLO: 'hello',                       // a tab announces itself
  HERE: 'here',                         // BroadcastChannel roll-call reply
  ACTIVE: 'active',                     // a tab's active project id changed
  PROJECTS_CHANGED: 'projects-changed', // the stored project set changed
  BYE: 'bye',                           // a tab is going away
  TABCOUNT: 'tabcount',                 // worker → tab: live tab count
  PEERS: 'peers',                       // worker → tab: active ids across tabs
});

// What happened to the project set, carried on a PROJECTS_CHANGED message so a
// tab viewing the same project can sync its editor (not just refresh the list).
export const PROJECT_ACTION = Object.freeze({
  UPDATED: 'updated',  // a project's content was saved → re-sync if it's ours
  REMOVED: 'removed',  // a project was deleted → drop to a blank editor if it's ours
  CLEARED: 'cleared',  // all projects deleted → drop to a blank editor
});
