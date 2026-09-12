/** The key a remote project meta carries (`remote: true`) once tagged. */
export declare const REMOTE_FLAG: string;

/** True for a loopback host, where plaintext http never leaves the machine. */
export declare function isLoopbackHost(host: string): boolean;

/** Canonical origin for a user-typed address; throws on one that cannot be a server URL. */
export declare function normalizeUrl(raw: string): string;

/** Split an invite link into { url, token }, or null when it is not one. */
export declare function parseInviteUrl(raw: string): { url: string; token: string } | null;

/** The shareable invite link for a server URL + token. */
export declare function buildInviteUrl(url: string, token: string): string;

/** True when an origin is plaintext http to a host that is not loopback. */
export declare function isInsecureRemote(origin: string): boolean;

/** The /ws endpoint for an http(s) origin. */
export declare function wsUrl(origin: string): string;

/** 401/403 — the credential was refused, as opposed to the server being absent. */
export declare function isAuthStatus(status: number): boolean;

/** The same question asked of a thrown REST error. */
export declare function isExpiredSession(err: unknown): boolean;
