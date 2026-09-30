// A project row's "Make a copy ›" entry: its three scopes, each opening the copy confirmation
// (ui/modal/copyProjectModal.js) grown out of the picked row and closing back into the "⋯" (up,
// once the projects window is closed).
import { COPY_SCOPES, COPY_SCOPE_LABELS, COPY_SCOPE_ICONS } from '../../../core/project/copy/options.js';

// The row's "⋯" as it stands at close time: a re-listed row has a fresh one.
const liveMenuButton = (anchor) => () => {
  if (anchor?.isConnected) return anchor;
  const id = anchor?.closest?.('.project-row')?.dataset.id;
  return id == null ? null : document.querySelector(`.project-row[data-id="${CSS.escape(id)}"] .project-more`);
};

// `target` is { id } for a local row or { remote } for a server-only one.
export const copyMenuItem = ({ target, anchor, onDone }) => ({
  icon: 'duplicate',
  label: 'Make a copy',
  items: COPY_SCOPES.map((what) => ({
    icon: COPY_SCOPE_ICONS[what],
    label: COPY_SCOPE_LABELS[what],
    onClick: (at) => document.querySelector('stencil-copy-project-modal')
      ?.openFor({ ...target, what, onDone }, { from: at, backTo: liveMenuButton(anchor) }),
  })),
});

// What the list does once a copy lands: an open here leaves the window, anything else re-lists.
export const afterRowCopy = ({ close, render, invalidateRemotes, scrollRowIntoView }) => (newId, how) => {
  if (how === 'here') { close(); return; }
  invalidateRemotes?.();
  render();
  if (newId != null) scrollRowIntoView?.(newId);
};
