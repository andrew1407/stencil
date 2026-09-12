// The set of servers one editor session is connected to: connect/disconnect/reconnect
// over ServerConnection, plus the expired-credential set the Servers UI shows as rows
// offering Reconnect. URL + status rules live in ./urlRules.js.
import type { ServerConnection, TaggedRemoteProject } from './serverConnection.js';
import type { SavedServer } from './connectionStore.js';

export {
  REMOTE_FLAG, isLoopbackHost, normalizeUrl, parseInviteUrl, buildInviteUrl,
  isInsecureRemote, wsUrl, isAuthStatus, isExpiredSession,
} from './urlRules.js';
export { ServerConnection } from './serverConnection.js';

/** A URL string, an invite link, or a saved entry; arrays connect each in turn. */
export type ConnectSpec = string | { url: string; token?: string; kind?: 'admin' | '' };

/** The stand-in row the list renders while a reconnect's handshake is in flight. */
export interface ReconnectingRow {
  url: string;
  status: 'connecting';
  connected: false;
  credentialKind: 'admin' | '';
}

export type ConnectionChange =
  | { type: 'connect' | 'disconnect' | 'reorder' }
  | { type: 'status' | 'expired'; connection: ServerConnection }
  | { type: 'event'; message: unknown; connection: ServerConnection };

export interface ConnectionManagerOptions {
  fetchImpl?: typeof fetch;
  WebSocketImpl?: typeof WebSocket;
  onChange?: (change: ConnectionChange) => void;
}

export declare class ConnectionManager {
  constructor(opts?: ConnectionManagerOptions);
  readonly urls: string[];
  readonly expiredUrls: string[];
  /** Live first, then the sessions that need a token, then any reconnect in flight. */
  readonly knownUrls: string[];
  readonly connections: ServerConnection[];
  readonly reconnectable: boolean;
  readonly last: ServerConnection | null;
  isExpired(url: string): boolean;
  /** Persistable view: the ORIGINAL credentials, expired sessions flagged. */
  snapshot(): SavedServer[];
  adoptExpired(entry?: { url: string; token?: string; kind?: string }): this;
  /** A USABLE connection exists here — an expired one does not count. */
  has(url: string): boolean;
  get(url: string): ServerConnection | ReconnectingRow | null;
  connect(spec: ConnectSpec | ConnectSpec[]): Promise<this>;
  disconnect(url?: string | null): this;
  disconnectAll(): this;
  reorder(orderedUrls: readonly string[] | null | undefined): this;
  /** `token` overrides the stored credential; '' mints a fresh session. */
  reconnectOne(url: string, token?: string): Promise<this>;
  reconnect(): Promise<this>;
  remoteProjects(): Promise<TaggedRemoteProject[]>;
}
