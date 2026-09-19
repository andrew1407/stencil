// The facts a project row states beside its name: the saved date, the hover text and how long
// the project has left. Read from the projects store, so both row kinds phrase them alike.
export function createRowMeta(store) {
  const fmtDate = ts => {
    if (!ts) return '';
    try {
      return new Date(ts).toLocaleString();
    } catch {
      return '';
    }
  };

  // The picture's size, its orientation and the description. No drawn-line length.
  const projectTooltip = meta => {
    const lines = [];
    const w = meta.imageW;
    const h = meta.imageH;
    if (w && h) lines.push(`${w}x${h} px · ${h >= w ? 'portrait' : 'landscape'}`);
    if (meta.description) lines.push(`Description: ${meta.description}`);
    return lines.join('\n');
  };

  const expiryLabel = meta => {
    if (store.isExpired(meta)) return { text: 'EXPIRED', expired: true, soon: false };
    const at = store.expiresAt(meta);
    if (at == null) return { text: '', expired: false, soon: false };
    const days = Math.max(0, Math.ceil((at - Date.now()) / (24 * 60 * 60 * 1000)));
    return {
      text: days <= 1 ? 'expires in 1 day' : `expires in ${days} days`,
      expired: false,
      soon: store.isExpiringSoon(meta),
    };
  };

  return { fmtDate, projectTooltip, expiryLabel };
}
