// What a saved project is made of: the live app → plain state readers. buildLayoutState
// feeds the pure serializeSession(); buildProjectMeta builds the registry row the projects
// list reads (never the image-heavy payload). Both read the app, neither writes it.
import type { DrawingApp } from './drawingApp.js';
import type { ProjectMeta } from './projectsStore.js';

/** The full session layout in LAYOUT_FIELDS order (config/layoutFields.json keys). */
export declare const buildLayoutState: (app: DrawingApp) => Record<string, unknown>;
/** The export subset the server push and ExportService both send. */
export declare const currentLayoutPayload: (app: DrawingApp) => Record<string, unknown>;
/**
 * `prev` carries the fields a plain save preserves (name, colour, expiry…); `layout` feeds
 * lineLengthCm; `thumbnail` (default: rendered inline now) lets the save path keep the last one.
 */
export declare const buildProjectMeta: (app: DrawingApp,
  opts: { prev?: Partial<ProjectMeta>; id: string; layout: Record<string, unknown>; thumbnail?: string | null }) => ProjectMeta;
