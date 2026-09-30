// What a copy carries, by scope: the original image always, with its blank colour and provenance;
// the layout from `layout` up; the project's own colour, words, expiry and chat for `project`.
import type { CopyScope } from './options.js';
import type { CopySource } from './source.js';
import type { ProjectMeta, ProjectLayout } from '../store/projectsStore.js';

export declare const copyScopes: (what: CopyScope) => { layout: boolean; meta: boolean; chat: boolean };
/** The new, detached row + payload; the thumbnail is left for the caller. */
export declare const copyPayload: (src: CopySource, what: CopyScope, at: { id: string | null; name: string; now?: number }) =>
  { meta: ProjectMeta; payload: { image: string | null; layout: Partial<ProjectLayout> } };
