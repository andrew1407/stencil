// SSRF guard for page-derived fetch URLs: <all_urls> lets fetch() reach any address on
// the user's network, and the URLs come from arbitrary pages. Lexical only — the same
// classes the CLI/pystencil/bot fetchers block; MV3 has no resolve-time hook for DNS rebinding.

// The URL parser already canonicalises octal/hex/decimal IPv4 forms into dotted-quad.
const parseIpv4 = (host) => {
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(host);
  if (!m) return null;
  const p = m.slice(1).map(Number);
  return p.every((n) => n <= 255) ? p : null;
};

// Bracketed or bare, `::` compression, trailing dotted IPv4, %zone → eight groups, or null.
const parseIpv6 = (host) => {
  let s = host.replace(/^\[|\]$/g, '');
  const pct = s.indexOf('%');
  if (pct >= 0) s = s.slice(0, pct);
  if (!s.includes(':') || !/^[0-9a-fA-F:.]+$/.test(s)) return null;
  if (s.includes('.')) {   // ::ffff:10.0.0.1 → fold the dotted tail into two groups
    const li = s.lastIndexOf(':');
    const v4 = parseIpv4(s.slice(li + 1));
    if (!v4) return null;
    s = `${s.slice(0, li + 1)}${((v4[0] << 8) | v4[1]).toString(16)}:${((v4[2] << 8) | v4[3]).toString(16)}`;
  }
  const dbl = s.indexOf('::');
  if (dbl !== s.lastIndexOf('::')) return null;
  let groups;
  if (dbl >= 0) {
    const head = s.slice(0, dbl) ? s.slice(0, dbl).split(':') : [];
    const tail = s.slice(dbl + 2) ? s.slice(dbl + 2).split(':') : [];
    if (head.length + tail.length > 7) return null;
    groups = [...head, ...Array(8 - head.length - tail.length).fill('0'), ...tail];
  } else {
    groups = s.split(':');
    if (groups.length !== 8) return null;
  }
  const out = [];
  for (const g of groups) {
    if (!/^[0-9a-fA-F]{1,4}$/.test(g)) return null;
    out.push(parseInt(g, 16));
  }
  return out;
};

const isBlockedV4 = ([a, b, c], allowLoopback) => {
  if (a === 127) return !allowLoopback;               // loopback
  return a === 0 ||                                   // 0.0.0.0/8 unspecified/this-net
    a === 10 ||                                       // private
    (a === 172 && b >= 16 && b <= 31) ||              // private
    (a === 192 && b === 168) ||                       // private
    (a === 169 && b === 254) ||                       // link-local, incl. 169.254.169.254
    (a === 100 && b >= 64 && b <= 127) ||             // CGNAT
    (a === 192 && b === 0 && (c === 0 || c === 2)) || // 192.0.0/24, TEST-NET-1
    (a === 198 && (b === 18 || b === 19)) ||          // 198.18/15 benchmarking
    (a === 198 && b === 51 && c === 100) ||           // TEST-NET-2
    (a === 203 && b === 0 && c === 113) ||            // TEST-NET-3
    a >= 224;                                         // multicast + reserved + broadcast
};

// The cloud metadata endpoint (and its IPv4-mapped IPv6 form) is never fetchable, even
// under the same-host carve-out.
const isMetadataHost = (host) => {
  const v4 = parseIpv4(host);
  if (v4) return v4[0] === 169 && v4[1] === 254 && v4[2] === 169 && v4[3] === 254;
  const g = (host.includes(':') || host.startsWith('[')) ? parseIpv6(host) : null;
  return !!g && g.slice(0, 5).every((x) => x === 0) && g[5] === 0xffff && g[6] === 0xa9fe && g[7] === 0xa9fe;
};

const isBlockedV6 = (g, allowLoopback) => {
  if (g[0] === 0 && g[1] === 0 && g[2] === 0 && g[3] === 0 && g[4] === 0 && g[5] === 0xffff)
    return isBlockedV4([g[6] >> 8, g[6] & 0xff, g[7] >> 8, g[7] & 0xff], allowLoopback);
  if (g.slice(0, 7).every((x) => x === 0)) {
    if (g[7] === 0) return true;                      // :: unspecified
    if (g[7] === 1) return !allowLoopback;            // ::1 loopback
  }
  return (g[0] & 0xffc0) === 0xfe80 ||                // fe80::/10 link-local
    (g[0] & 0xffc0) === 0xfec0 ||                     // fec0::/10 site-local (deprecated)
    (g[0] & 0xfe00) === 0xfc00;                       // fc00::/7 ULA
};

// data:/blob: pass; http(s) passes unless the host is a private/loopback/link-local/
// CGNAT/ULA/unspecified literal or localhost; every other scheme is refused.
// `allowLoopback` only for URLs the USER typed. `allowSameHostAs` is the TRUSTED page URL
// (sender.tab.url / a scan-recorded resource, never page-supplied): the browser already
// talks to that host, so fetching it is no escalation — except the metadata IP.
export const isAllowedImageUrl = (url, { allowLoopback = false, allowSameHostAs = '' } = {}) => {
  let u;
  try { u = new URL(String(url)); } catch { return false; }
  if (u.protocol === 'data:' || u.protocol === 'blob:') return true;
  if (u.protocol !== 'http:' && u.protocol !== 'https:') return false;
  const host = u.hostname.toLowerCase();
  if (allowSameHostAs && !isMetadataHost(host)) {
    try {
      const p = new URL(String(allowSameHostAs));
      if ((p.protocol === 'http:' || p.protocol === 'https:') && p.hostname.toLowerCase() === host) return true;
    } catch { /* no usable page context — stay strict */ }
  }
  if (host === 'localhost' || host.endsWith('.localhost')) return allowLoopback;
  if (host.includes(':') || host.startsWith('[')) {     // IPv6 literal
    const g = parseIpv6(host);
    return !!g && !isBlockedV6(g, allowLoopback);       // unparseable literal → refuse
  }
  const v4 = parseIpv4(host);
  if (v4) return !isBlockedV4(v4, allowLoopback);
  return true;   // a public name — private DNS answers are invisible lexically (MV3)
};
