// The console line grammar, a port of cli/src/console/commands.zig: an optional leading '/',
// the first word is the command, the rest its argument. Pure; no session state.
export const slashed = (line) => String(line ?? '').trim().startsWith('/');

export const parseCommandLine = (line) => {
  let s = String(line ?? '').trim();
  const wasSlashed = s.startsWith('/');
  if (wasSlashed) s = s.slice(1).trimStart();
  if (!s) return { word: '', arg: '', slashed: wasSlashed };
  const sp = s.search(/\s/);
  if (sp < 0) return { word: s, arg: '', slashed: wasSlashed };
  return { word: s.slice(0, sp), arg: s.slice(sp + 1).trim(), slashed: wasSlashed };
};

export const splitWords = (arg) => String(arg ?? '').split(/[\s,]+/).filter(Boolean);

// A `/connect` word that reads as a server URL rather than a token: a scheme, a dot or a port,
// or localhost. Each URL starts a connection and one following token word attaches to it.
export const looksLikeServerUrl = (word) =>
  word.includes('://') || /[.:]/.test(word) || word.toLowerCase() === 'localhost';

export const parseConnectArgs = (arg) => {
  const out = [];
  for (const word of splitWords(arg)) {
    const last = out[out.length - 1];
    if (last && last.token == null && !looksLikeServerUrl(word)) last.token = word;
    else out.push({ url: word, token: null });
  }
  return out;
};

// `x1=10% x2=90% y1=5cm album` → the crop spec the facade takes plus the album flag.
export const parseCropSpec = (arg) => {
  const spec = {};
  for (const tok of String(arg ?? '').split(/\s+/).filter(Boolean)) {
    const low = tok.toLowerCase();
    if (low === 'album' || low === '--album') { spec.album = true; continue; }
    const m = /^(x1|x2|y1|y2|aspect)=(.+)$/i.exec(tok);
    if (!m) return null;
    spec[m[1].toLowerCase()] = m[2];
  }
  return spec;
};
