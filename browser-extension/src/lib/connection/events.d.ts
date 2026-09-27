// Shapes for lib/connection/events.js — the server's global project feed over /ws.
export interface ProjectEvent { type: 'project-event'; event: 'created' | 'updated' | 'deleted'; project?: { id: string } }
export interface EventsSubscription { close(): void; }

export declare const eventsUrl: (serverUrl: string) => string;
export declare const subscribeProjectEvents: (
  conn: { url: string; token: string },
  onEvent: (msg: ProjectEvent) => void,
  opts?: { WebSocket?: typeof WebSocket; onState?: (live: boolean) => void },
) => EventsSubscription | null;
