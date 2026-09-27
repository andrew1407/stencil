// The SSRF address table as a judge: `blockedRanges.json` is a checked-in copy of
// browser/js/config/net/blockedRanges.json (tests/dataParity.test.js pins it), and
// browser/js/config/fixtures/net/hosts.json is the corpus it answers to. Addresses are
// byte arrays: 4 for IPv4, 16 for IPv6.
import TABLE from './blockedRanges.json' with { type: 'json' };

export const parseIpv4 = (host) => {
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(host);
  if (!m) return null;
  const p = m.slice(1).map(Number);
  return p.every((n) => n <= 255) ? p : null;
};

// Bracketed or bare, `::` compression, a trailing dotted IPv4 and a %zone → 16 bytes, or null.
export const parseIpv6 = (host) => {
  let s = host.replace(/^\[|\]$/g, '');
  const pct = s.indexOf('%');
  if (pct >= 0) s = s.slice(0, pct);
  if (!s.includes(':') || !/^[0-9a-fA-F:.]+$/.test(s)) return null;
  if (s.includes('.')) {
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
  if (!groups.every((g) => /^[0-9a-fA-F]{1,4}$/.test(g))) return null;
  return groups.flatMap((g) => { const n = parseInt(g, 16); return [n >> 8, n & 0xff]; });
};

export const parseAddress = (host) => parseIpv4(host) || parseIpv6(host);

const parseCidr = (cidr) => {
  const [addr, bits] = cidr.split('/');
  return { bytes: parseAddress(addr), prefix: Number(bits) };
};

const inCidr = (bytes, { bytes: net, prefix }) => {
  if (bytes.length !== net.length) return false;
  for (let i = 0, left = prefix; left > 0; i++, left -= 8) {
    const mask = left >= 8 ? 0xff : (0xff << (8 - left)) & 0xff;
    if ((bytes[i] & mask) !== (net[i] & mask)) return false;
  }
  return true;
};

const CLASSES = new Map(Object.entries(TABLE.classes).map(([name, cidrs]) => [name, cidrs.map(parseCidr)]));
const EMBEDS = TABLE.embedsV4.map((r) => ({
  net: parseCidr(r.prefix), offset: r.offset, except: (r.except || []).map(parseCidr),
}));

// The IPv4 address an IPv6 form carries (mapped, compatible, NAT64, 6to4), else the address itself.
export const carriedV4 = (bytes) => {
  if (bytes.length !== 16) return bytes;
  const rule = EMBEDS.find((r) => inCidr(bytes, r.net) && !r.except.some((e) => inCidr(bytes, e)));
  return rule ? bytes.slice(rule.offset, rule.offset + 4) : bytes;
};

export const inClass = (bytes, name) => {
  const judged = carriedV4(bytes);
  return CLASSES.get(name).some((cidr) => inCidr(judged, cidr));
};

// `options` holds the policy's `blocksUnless` switches, e.g. { allowLoopback: true }.
export const isBlockedAddress = (bytes, policy, options = {}) => {
  const p = TABLE.policies[policy];
  const unless = Object.entries(p.blocksUnless || {}).flatMap(([opt, names]) => (options[opt] ? [] : names));
  return [...p.blocks, ...unless].some((name) => inClass(bytes, name));
};
