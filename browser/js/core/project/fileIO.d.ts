// The session side of .stencil project files: gather this session's state for the pure
// (de)serializer in file.js, apply a parsed file (as a new project or in place),
// prompt on a live-sync conflict, and paint the live-sync button.
import type { DrawingApp } from '../drawingApp.js';

/** A parsed .stencil document as applyProjectFile / applyProjectFileInPlace read it. */
export interface ProjectFileData {
  name?: string;
  color?: string;
  description?: string;
  keywords?: string[];
  source?: string;
  resource?: string;
  blank?: boolean;
  blankColor?: string;
  image?: { dataUrl: string; ext?: string; w?: number; h?: number };
  layout?: Record<string, unknown> & { lines?: unknown[]; rotationQuarters?: number; cropRect?: { x: number; y: number; w?: number; h?: number; width?: number; height?: number } };
  theme?: { mode?: string; accent?: string };
}

/** The state buildProjectFile wants: ORIGINAL image + export layout + metadata (+ theme). */
export interface ProjectFileState {
  name: string;
  color: string;
  keywords: string[];
  source: string;
  resource: string;
  blank: boolean;
  blankColor: string;
  layout: Record<string, unknown>;
  image?: { dataUrl: string; ext: string; w: number; h: number };
  theme?: { mode: string; accent: string };
}

export declare const projectFileState: (app: DrawingApp, opts?: { includeTheme?: boolean }) => ProjectFileState;
/** Opens the file as a NEW local project (flush → reset → load); resolves to the project name. */
export declare const applyProjectFile: (app: DrawingApp, project: ProjectFileData) => Promise<string>;
/** Live file sync: replaces the CURRENT project's layout; `mergeLines` unions on a conflict. */
export declare const applyProjectFileInPlace: (app: DrawingApp, project: ProjectFileData, opts?: { mergeLines?: boolean }) => void;
export type FileConflictChoice = 'theirs' | 'merge' | 'mine';
export declare const chooseFileConflict: (app: DrawingApp, name?: string) => Promise<FileConflictChoice>;
export declare const updateStencilSyncUI: (app: DrawingApp) => void;
