// Two small goldens core pins its hand-written twins against. grammarProbes: strings run through the
// registry's regexes with JS RegExp semantics (core/opplan/planGrammars matches them by hand). numbers:
// doubles by bit pattern with their JS String() form (core/json/jsNumber's jsNumberToString).

const CURATED = Object.freeze([
  '', ' ', '-', '.', '0', '1', '-1', '.5', '-.5', '1.', '1.5', '1..5', '1.5.5', '10%', '10 %', '-10%',
  '10px', '10PX', '10cm', '10in', '10pt', '+1', '--1', '1e2', '-0', '00.00', '0.0cm', '%', 'px',
  '\u0663', '\uff13', '\u0661\u0660%', '1\u00a0', '4:3', '04:3', '0:3', '4:0', '00:00', '1:01', '1:1:1',
  ' 1:1', '1:1 ', '1.5:2', 'a:b', '\uff14:\uff13', 'a0', 'a4', 'a10', 'a11', 'b10', 'c9', 'd4', 'A4',
  'a01', 'a', 'a 4', 'a4 ', 'a\uff14', '#aabbcc', '#AABBCC', '#aabbc', '#aabbccd', 'aabbcc', '#gggggg',
  '#aabbcc\n', 'red', 'Red', 'hot pink', 'red1', 'r\u00e9d', '\u212aelvin', 'x', 'y', 'x*2', 'x^2',
  'x+y', '2*(x+1)', 'x**2', '1e3', 'x\n', 'x\u00a0', '\uff11', 'y*2 + 1', 'http://a', 'https://a',
  'HTTP://A', 'hTtPs://a/b', 'https://', 'http:/a', 'ftp://a', 'https://a b', 'https://a\u00a0b',
  'https://a\u2028', 'https://a\ud800', 'https://\ud83d\ude00', ' https://a', 'https://a\t',
  'https://a\u200b', 'https://a\u180e', 'https://a\u0085', 'https://a\ufeff', 'https://a\u3000',
  'https://a\u1680', 'https://a\u202f', 'https://a\u205f', 'https://a\u2029', 'https://a\u000b',
  'a://', 'a+b.c-d://x', '1a://', '://', 'a:/b', 'C:\\x', '~/x', 'file:///x', '\u00e9://', 'a b://',
  'z9+.-://', 'x\ud800', '\udc00',
]);
const ALPHABET = Object.freeze(['0', '1', '9', '.', '-', '%', 'p', 'x', 'c', 'm', 'i', 'n', ':']);

export const grammarProbes = (registry) => {
  const names = Object.keys(registry.regexes).filter((n) => n !== 'describe' && n !== 'note');
  const res = names.map((n) => [n, new RegExp(registry.regexes[n])]);
  const probes = new Set(CURATED);
  for (const a of ALPHABET) for (const b of ALPHABET) { probes.add(a + b); for (const c of ALPHABET) probes.add(a + b + c); }
  for (let c = 0x20; c < 0x7f; c++) probes.add(String.fromCharCode(c));
  const rows = [...probes].map((s) => [s, res.filter(([, re]) => re.test(s)).map(([n]) => n)]);
  return { regexes: Object.fromEntries(names.map((n) => [n, registry.regexes[n]])), probes: rows };
};

const view = new DataView(new ArrayBuffer(8));
const bitsOf = (x) => { view.setFloat64(0, x); return view.getBigUint64(0).toString(16).padStart(16, '0'); };
const fromBits = (b) => { view.setBigUint64(0, BigInt.asUintN(64, b)); return view.getFloat64(0); };
const next = (x, dir) => { view.setFloat64(0, x); return fromBits(view.getBigUint64(0) + BigInt(dir)); };

// Every power of two with the double just below it (the shortest form is asymmetric at a
// binade's bottom), powers of ten, the toString regime edges, the specials and a fixed pseudo-random spread.
export const numberCases = () => {
  const xs = [0, -0, NaN, Infinity, -Infinity, Number.MAX_VALUE, Number.MIN_VALUE, Number.EPSILON,
    2 ** 53, 2 ** 53 + 2, 1e21, 999999999999999900000, 1e-6, 1e-7, 0.1, 0.2, 0.3, 1 / 3, 123e-20, -1.5, 5e-324];
  for (let e = -1074; e <= 1023; e++) {
    const p = 2 ** e;
    xs.push(p);
    if (e > -1074) xs.push(next(p, -1));
  }
  for (let k = -325; k <= 309; k++) xs.push(Number(`1e${k}`));
  let seed = 0x9e3779b97f4a7c15n;
  for (let i = 0; i < 1000; i++) {
    seed = BigInt.asUintN(64, seed * 6364136223846793005n + 1442695040888963407n);
    const x = fromBits(seed);
    if (Number.isFinite(x)) xs.push(x);
  }
  const seen = new Set();
  return xs.map((x) => [bitsOf(x), String(x)]).filter(([b]) => !seen.has(b) && seen.add(b));
};
