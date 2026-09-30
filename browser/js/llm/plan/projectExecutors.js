// ── The §10 project-copy executor ───────────────────────────────
// "Make a copy" through the same core path as the projects menu, the canvas menu and the
// toolbar; a refused or reduced copy is a note, never a failed plan.

export const PROJECT_RUN = Object.freeze({
  copyProject: async (a, { copyActiveProject, notes }) => {
    if (!copyActiveProject) throw new Error('This surface cannot copy projects');
    const note = await copyActiveProject({
      what: a.what, open: a.open || 'none', incognito: !!a.incognito, local: !!a.local,
    });
    if (note) notes?.push(`copyProject: ${note}`);
  },
});
