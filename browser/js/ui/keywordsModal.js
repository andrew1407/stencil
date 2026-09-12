import { createProjectMetaModal } from './projectMetaModal.js';

// The active project's search keywords, comma or space separated (the store normalizes).
// Gated on a saved, non-incognito project.
export const StencilKeywordsModal = createProjectMetaModal({
  name: 'keywords',
  title: 'Project keywords',
  glyph: 'keywords',
  rows: 3,
  placeholder: 'keyword, another keyword…',
  hint: 'Comma or space separated · used by the projects search.',
  noun: 'keywords',
  addLabel: 'keywords',
  load: (meta) => (meta?.keywords || []).join(', '),
  save: (app, id, value) => app.setProjectKeywords(id, value.split(/[\s,]+/)),
  // A keyword list is one line of thought: Enter saves (Shift+Enter still breaks the line).
  commitsOn: (e) => !e.shiftKey,
});
