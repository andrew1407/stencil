// Resolving a project by NAME for the §10 project ops. Project names are unique
// (projectsStore.nameExists), so a batch of saves wanting the same base is suffixed
// until free rather than losing the save to a clash.
import type { DrawingApp } from '../core/drawingApp.js';
import type { ProjectMeta } from '../core/project/store/projectsStore.js';

/** Exactly one of the two: the resolved record, or why nothing resolved. */
export type ProjectNameResolution =
  | { meta: ProjectMeta; note?: undefined }
  | { note: string; meta?: undefined };

/** `wanted`, or `wanted 2`, `wanted 3`, … — the first name no saved project uses. */
export declare const uniqueProjectName: (app: DrawingApp, wanted: string) => string;
/** Exact name among the SAVED local projects, else a unique case-insensitive prefix. */
export declare const resolveProjectByName: (app: DrawingApp, name: string) => ProjectNameResolution;
