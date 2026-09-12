// ── Resolving a project by NAME, for the §10 project ops ────────────────────
// Project names are unique (projectsStore.nameExists), and a batch of saves routinely
// wants the same base — suffix until it is free rather than losing the save to a clash.
export const uniqueProjectName = (app, wanted) => {
  const store = app?.storage?.store;
  if (!store?.nameExists) return wanted;
  if (!store.nameExists(wanted)) return wanted;
  for (let n = 2; n < 1000; n++) {
    const candidate = `${wanted} ${n}`;
    if (!store.nameExists(candidate)) return candidate;
  }
  return wanted;
};

// §10 name resolution, shared by removeProject/openProject: exact name match among
// the SAVED local projects, else a unique case-insensitive prefix. Returns the meta
// record, or a note string explaining why nothing resolved.
export const resolveProjectByName = (app, name) => {
  const list = app.storage.store.list();
  const exact = list.filter((m) => (m.name || '') === name);
  const picks = exact.length
    ? exact
    : list.filter((m) => (m.name || '').toLowerCase().startsWith(name.toLowerCase()));
  if (!picks.length) return { note: `no saved project named "${name}"` };
  if (picks.length > 1) return { note: `"${name}" matches ${picks.length} projects — use the full name` };
  return { meta: picks[0] };
};
