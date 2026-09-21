import type { ProjectRowGesture } from './actions.js';

/** One saved project's metadata record, as the projects store holds it. */
export interface ProjectMetaLike {
  id: string;
  name?: string;
  [field: string]: unknown;
}

/**
 * Swap the row's name element for the inline rename editor. Resolves nothing: the editor
 * commits through `app.renameProject` and asks for a re-`render()` either way.
 */
export function beginRowRename(opts: {
  meta: ProjectMetaLike;
  name: HTMLElement;
  app: unknown;
  render: () => void;
  gesture?: ProjectRowGesture | null;
}): void;
