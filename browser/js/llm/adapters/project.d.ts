// §10 project adapters: resolve a project by NAME, then run the projects modal's own
// guarded flows (their confirms included). Each resolves to a note string when nothing
// happened (unknown / ambiguous / declined) and to `null` when it did the work.
import type { DrawingApp } from '../../core/drawingApp.js';

export interface ProjectAdapters {
  removeProjectNamed(name: string): Promise<string | null>;
  /** removeProject current:true with nothing saved — the `clear` flow behind the same confirm. */
  clearWorkingImage(): Promise<string | null>;
  /** `last` opens the most recently edited project instead of resolving `name`. */
  openProjectNamed(name: string, last?: boolean): Promise<string | null>;
  renameActiveProject(name: string): Promise<string | null>;
  /** Valid only on a BLANK project; CSS colour names resolve to hex first. */
  setBlankColor(color: string): Promise<string | null>;
  /** Every saved local project, or every one but the open one with `keepCurrent`. */
  clearLocalProjects(keepCurrent?: boolean): Promise<string | null>;
}

export declare const projectAdapters: (app: DrawingApp) => ProjectAdapters;
