// "Make a copy" requests, shared by the projects menu, the canvas menu, the toolbar, the facade
// and the copyProject op (desktop twin: app/project/copy/ProjectCopy.cpp): the three scopes, the
// ways a copy opens, and the one rule set that reduces a request to what can be honoured.

export const COPY_SCOPES = Object.freeze(['image', 'layout', 'project']);
export const COPY_OPENS = Object.freeze(['none', 'here', 'newtab']);

export const COPY_SCOPE_LABELS = Object.freeze({
  image: 'Image only', layout: 'Image and layout', project: 'Whole project',
});
export const COPY_SCOPE_ICONS = Object.freeze({ image: 'image', layout: 'layers', project: 'folder' });

// A server source copies onto its server unless `local`; only a local copy may stay unsaved
// (incognito), and an incognito copy exists only once it is opened. `note` names what was dropped.
export const settleCopyOptions = ({ what, open = 'none', incognito = false, local = false } = {},
  { serverSource = false } = {}) => {
  if (!COPY_SCOPES.includes(what)) throw new Error(`Unknown copy scope "${what}" — one of ${COPY_SCOPES.join(', ')}`);
  if (!COPY_OPENS.includes(open)) throw new Error(`Unknown copy open "${open}" — one of ${COPY_OPENS.join(', ')}`);
  const onServer = serverSource && !local;
  const notes = [];
  let keepIncognito = !!incognito;
  if (keepIncognito && open === 'none') { keepIncognito = false; notes.push('an incognito copy must be opened — saved it instead'); }
  if (keepIncognito && onServer) { keepIncognito = false; notes.push('a server copy cannot be incognito — made it on the server'); }
  return { what, open, incognito: keepIncognito, onServer, note: notes.join('; ') || null };
};
