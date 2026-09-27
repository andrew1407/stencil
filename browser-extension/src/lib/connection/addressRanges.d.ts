// Shapes for lib/connection/addressRanges.js — the SSRF address table as a judge.
/** An address as bytes: 4 for IPv4, 16 for IPv6. */
export type AddressBytes = number[];
export type Policy = 'fetch' | 'serverTarget';

/** Dotted-quad only, as the URL parser serialises IPv4. */
export declare function parseIpv4(host: string): AddressBytes | null;
/** Bracketed or bare, `::` compression, a trailing dotted IPv4, a %zone. */
export declare function parseIpv6(host: string): AddressBytes | null;
export declare function parseAddress(host: string): AddressBytes | null;
/** The IPv4 address an IPv6 form carries (mapped, compatible, NAT64, 6to4), else the address. */
export declare function carriedV4(bytes: AddressBytes): AddressBytes;
export declare function inClass(bytes: AddressBytes, name: string): boolean;
/** `options` holds the policy's `blocksUnless` switches: `allowLoopback`, `allowPrivate`. */
export declare function isBlockedAddress(
  bytes: AddressBytes, policy: Policy, options?: { allowLoopback?: boolean; allowPrivate?: boolean },
): boolean;
