import { createProjectMetaModal } from './projectMetaModal.js';

// ── Component: project description modal ─────────────────────────
// The ACTIVE project's free-text description (shown in the projects list row and its
// tooltip). Save writes through app.setProjectDescription — the same store write the
// projects list's "Add description" row item makes. Shell, gating and commit/discard
// come from projectMetaModal.js; the button is gated on a saved, non-incognito project
// (ui/controlState.js).
export const StencilDescriptionModal = createProjectMetaModal({
  name: 'description',
  title: 'Project description',
  glyph: 'description',
  rows: 6,
  placeholder: 'Describe this project…',
  hint: 'Shown in the projects list and its tooltip.',
  noun: 'description',
  addLabel: 'a description',
  load: (meta) => meta?.description || '',
  save: (app, id, value) => app.setProjectDescription(id, value),
  // Enter keeps its newline in a multi-line field; Ctrl/Cmd+Enter commits.
  commitsOn: (e) => e.ctrlKey || e.metaKey,
});
