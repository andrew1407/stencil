// Project names and keywords, over a plain array of registry metas: the duplicate-name
// guard, the copy-suffix numbering, the "same image" match and the keyword normalizer.

// Trim, drop blanks, dedupe case-insensitively (first-seen order). Matches the server's
// joinKeywords so a keyword set round-trips identically.
export const normalizeKeywords = (keywords) => {
  const out = [];
  const seen = new Set();
  for (const raw of (Array.isArray(keywords) ? keywords : [])) {
    const k = String(raw == null ? '' : raw).trim();
    if (!k) continue;
    const lk = k.toLowerCase();
    if (seen.has(lk)) continue;
    seen.add(lk);
    out.push(k);
  }
  return out;
};

// Strip a trailing " (N)" so "photo (2)" and "photo" group together.
export const baseProjectName = (name) =>
  String(name || '').replace(/\s*\(\d+\)\s*$/, '').trim();

// Trimmed, case-insensitive; drives the "no duplicate names" guard in renameProject.
export const nameExists = (metas, name, exceptId = null) => {
  const n = String(name || '').trim().toLowerCase();
  if (!n) return false;
  return metas.some(m =>
    m && m.id !== exceptId && String(m.name || '').trim().toLowerCase() === n);
};

// → { ok, reason }; gates the rename ✓ button. `exceptId` is the project being renamed.
export const validateName = (metas, name, exceptId = null) => {
  const clean = String(name || '').trim();
  if (!clean) return { ok: false, reason: 'Name can’t be empty' };
  if (clean.length > 80) return { ok: false, reason: 'Name is too long (max 80 characters)' };
  if (nameExists(metas, clean, exceptId)) return { ok: false, reason: `“${clean}” is already taken` };
  return { ok: true, reason: '' };
};

// Same image = identical non-empty `source` URL, else a base-name match. Drives the
// extension-launch "resume" path + copy-numbering.
export const findByImage = (metas, source, name) => {
  const src = source || '';
  const base = baseProjectName(name || '');
  return metas.filter(m => {
    if (src) return (m.source || '') === src;
    return !!base && baseProjectName(m.name || '') === base;
  });
};

// Next free "Name (N)" for a copy: the bare base name when free, else the lowest unused N ≥ 1.
export const copyName = (metas, baseName, source) => {
  const base = baseProjectName(baseName || '') || (baseName || 'Untitled');
  const taken = new Set(findByImage(metas, source, base).map(m => m.name || ''));
  if (!taken.has(base)) return base;
  let n = 1;
  while (taken.has(`${base} (${n})`)) n++;
  return `${base} (${n})`;
};

export const defaultName = (metas) => {
  let max = 0;
  for (const m of metas) {
    const match = /^Untitled (\d+)$/.exec((m && m.name) || '');
    if (match) max = Math.max(max, parseInt(match[1], 10));
  }
  return `Untitled ${max + 1}`;
};
