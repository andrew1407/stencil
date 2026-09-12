// SharedWorker entry point — no exports; self.onconnect is its whole surface. Pure message
// router between tabs (worker/messages.js MSG); persistence stays window-side.
export interface TabcountMsg { type: 'tabcount'; count: number; youAreOnly: boolean; }
export interface PeersMsg { type: 'peers'; activeIds: unknown[]; }
export interface IncognitosMsg { type: 'incognitos'; sessions: { name: string; updatedAt: number }[]; }
