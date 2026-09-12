import { createProjectMetaModal } from './projectMetaModal.js';

// The active project's description; saves through app.setProjectDescription, the same
// store write the projects list's row item makes. Gated on a saved, non-incognito project.
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
