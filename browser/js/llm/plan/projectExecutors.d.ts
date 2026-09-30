// The §10 project-copy executor: "Make a copy" through the same core path as the projects
// menu, the canvas menu and the toolbar; a refused or reduced copy is a note.
import type { OpRunner } from './opExecutors.js';

export type ProjectOpName = 'copyProject';

export declare const PROJECT_RUN: Record<ProjectOpName, OpRunner>;
