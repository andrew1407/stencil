/** data:/blob: pass; http(s) passes unless the host is private/loopback/link-local/CGNAT/ULA. */
export declare function isAllowedImageUrl(
  url: string, opts?: { allowLoopback?: boolean; allowSameHostAs?: string },
): boolean;
