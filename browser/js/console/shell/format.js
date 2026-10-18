// The tables the console prints: projects (local and server), connections, and the column
// padding they share. Pure over plain rows.
export const padColumns = (rows) => {
  const widths = [];
  for (const r of rows) r.forEach((cell, i) => { widths[i] = Math.max(widths[i] ?? 0, String(cell).length); });
  return rows.map((r) => r.map((cell, i) => (i === r.length - 1 ? String(cell) : String(cell).padEnd(widths[i]))).join('  '));
};

const when = (ms) => (Number.isFinite(ms) && ms > 0 ? new Date(ms).toISOString().slice(0, 16).replace('T', ' ') : '');

// `long` adds the size, colour and date columns (`ll`); the short form is name and where it lives.
export const projectsRows = (local = [], remote = [], { long = false, activeId = null } = {}) => {
  const rows = [];
  for (const m of local) {
    const mark = m.id === activeId ? '*' : ' ';
    const row = [`${mark} ${m.name || 'Untitled'}`, m.address ? `server ${m.address}` : 'local'];
    if (long) row.push(m.imageWidth && m.imageHeight ? `${m.imageWidth}x${m.imageHeight}` : '', m.color || '', when(m.updatedAt));
    rows.push(row);
  }
  for (const m of remote) rows.push([`  ${m.name || 'Untitled'}`, `server ${m.serverUrl}`, ...(long ? ['', m.color || '', when(m.updatedAt)] : [])]);
  return rows.length ? padColumns(rows) : ['(no projects)'];
};

export const connectionsRows = (urls = []) => (urls.length ? urls.map((u) => `  ${u}`) : ['(no server connections)']);
