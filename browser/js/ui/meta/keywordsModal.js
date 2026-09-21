import { createProjectMetaModal } from './projectMetaModal.js';
import { keywordChipsField } from './keywordChips.js';

// The active project's search keywords, added one at a time and shown as chips.
// Gated on a saved, non-incognito project.
export const StencilKeywordsModal = createProjectMetaModal({
  name: 'keywords',
  title: 'Project keywords',
  glyph: 'keywords',
  hint: '',
  noun: 'keywords',
  addLabel: 'keywords',
  field: keywordChipsField({ placeholder: 'Add a keyword…' }),
  load: (meta) => meta?.keywords || [],
  save: (app, id, value) => app.setProjectKeywords(id, value),
});
