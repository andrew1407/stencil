// Pure .stencil project-file (de)serializer: a JSON bundle of a whole project (ORIGINAL
// image + export layout + metadata + optional theme). parseProjectFile hardens every
// field; a file over MAX_PROJECT_FILE_CHARS is refused before JSON.parse.
import type { LayoutPayload } from '../layout.js';

export declare const STENCIL_FILE_FORMAT: 'stencil-project';
export declare const STENCIL_FILE_VERSION: 1;
/** Chars ≈ bytes; matches the server's 32 MiB MaxBodyBytes. */
export declare const MAX_PROJECT_FILE_CHARS: number;

export interface ProjectFileImage { dataUrl: string; ext: string; w?: number; h?: number; }
export interface ProjectFileTheme { mode?: 'light' | 'dark'; accent?: string; }

/** The document on disk: `format` is the sentinel, `version` the schema. */
export interface ProjectFileDoc {
  format: 'stencil-project';
  version: number;
  name: string;
  color?: string;
  keywords?: string[];
  source?: string;
  resource?: string;
  blank?: true;
  blankColor?: string;
  image?: ProjectFileImage;
  layout: LayoutPayload;
  theme?: ProjectFileTheme;
}

/** What the editor hands buildProjectFile; empty fields are omitted from the document. */
export interface ProjectFileState {
  name?: string;
  color?: string;
  keywords?: string[];
  source?: string;
  resource?: string;
  blank?: boolean;
  blankColor?: string;
  image?: Partial<ProjectFileImage> | null;
  layout?: Record<string, unknown>;
  theme?: Partial<ProjectFileTheme> | null;
}

/** The hardened shape DrawingApp.applyProjectFile consumes. */
export interface ParsedProject {
  name: string;
  color: string;
  keywords: string[];
  source: string;
  resource: string;
  blank: boolean;
  blankColor: string;
  image: ProjectFileImage;
  layout: LayoutPayload;
  theme: ProjectFileTheme | null;
}

export type ProjectFileParse = { ok: true; project: ParsedProject } | { ok: false; error: string };

export declare const buildProjectFile: (state?: ProjectFileState) => ProjectFileDoc;
export declare const serializeProjectFile: (state: ProjectFileState) => string;
export declare const parseProjectFile: (input: string | unknown) => ProjectFileParse;
