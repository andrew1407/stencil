// The rows that stand in for a real one: the server-listing skeletons, another tab's incognito
// session, and the caption when a filter lists nothing at all.
import { icon } from '../icons.js';

// Shown while the server listing loads, so the modal opens instantly.
export const makeSkeletonRow = () => {
  const row = document.createElement('div');
  row.className = 'project-row project-skeleton';
  row.innerHTML = '<div class="project-thumb skel"></div>'
    + '<div class="project-info"><div class="skel skel-line"></div>'
    + '<div class="skel skel-line short"></div></div>';
  return row;
};

// A read-only row for an incognito session open in another tab.
export const makeIncognitoPeerRow = (p) => {
  const row = document.createElement('div');
  row.className = 'project-row project-incognito';
  const thumb = document.createElement('div');
  thumb.className = 'project-thumb project-thumb-placeholder';
  thumb.innerHTML = icon('incognito', { size: 24 });
  row.appendChild(thumb);
  const info = document.createElement('div');
  info.className = 'project-info';
  const name = document.createElement('div');
  name.className = 'project-name';
  name.textContent = p.name || 'Incognito (unsaved)';
  const sub = document.createElement('div');
  sub.className = 'project-sub';
  sub.textContent = 'Incognito · open in another tab';
  info.append(name, sub);
  row.appendChild(info);
  return row;
};

export const emptyLabelFor = (mode) =>
  mode === 'incognito' ? 'No incognito tabs.'
    : mode === 'server' ? 'No server projects.'
      : mode === 'local' ? 'No local projects.'
        : mode === 'peer-open' ? 'Nothing open in another tab.'
          : mode === 'peer-closed' ? 'Every saved project is open in another tab.'
            : 'No saved projects yet.';
