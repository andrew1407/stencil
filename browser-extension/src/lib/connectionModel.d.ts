export declare const CONNECTIONS_KEY: string;
export declare const isLoopbackHost: (host: string) => boolean;
export declare const normalizeUrl: (raw: string) => string;
export interface Invite { url: string; token: string; }
export declare const parseInviteUrl: (raw: string) => Invite;

export interface ServerProject {
  id: string; name?: string; source?: string; resource?: string; color?: string;
  updatedAt?: number; hasImage?: boolean;
}
export interface SharedPin {
  source: string; origin: string; site: string; resource: string; name: string; color: string;
  kind: 'image'; t: number; shared: true; serverUrl: string; projectId: string;
}
export declare const sharedPinFromProject: (proj: ServerProject, serverUrl: string) => SharedPin;
export declare const sharedPinsFromProjects: (projects: ServerProject[], serverUrl: string) => SharedPin[];
export declare const mergePins: (local: Record<string, unknown>[], shared: SharedPin[]) => Record<string, unknown>[];
