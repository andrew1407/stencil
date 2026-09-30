// A project row's "Make a copy ›" entry: its three scopes, each opening the copy confirmation.
import type { ProjectRowMenuItem } from '../window/projectRowMenu.js';
import type { CopyTarget } from '../../modal/copyProjectModal.js';
import type { CopyOpen } from '../../../core/project/copy/options.js';

export declare const copyMenuItem: (opts: {
  target: Pick<CopyTarget, 'id' | 'remote'>;
  anchor: Element;
  onDone: CopyTarget['onDone'];
}) => ProjectRowMenuItem;

/** An open here leaves the projects window; anything else re-lists and scrolls to the new row. */
export declare const afterRowCopy: (deps: {
  close: () => void;
  render: () => void;
  invalidateRemotes?: () => void;
  scrollRowIntoView?: (id: string) => void;
}) => (newId: string | null, how: CopyOpen) => void;
