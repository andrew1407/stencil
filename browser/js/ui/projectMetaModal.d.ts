import type { DrawingApp } from '../core/drawingApp.js';
import type { StencilElement } from './base.js';

export interface ProjectMetaModalSpec {
  name: string;
  title: string;
  glyph: string;
  rows: number;
  placeholder: string;
  hint: string;
  noun: string;
  addLabel: string;
  load: (meta: Record<string, unknown> | null) => string;
  save: (app: DrawingApp, id: string | number, value: string) => unknown;
  commitsOn: (e: KeyboardEvent) => boolean;
}

/** One textarea modal factory shared by descriptionModal.js and keywordsModal.js. */
export declare const createProjectMetaModal: (spec: ProjectMetaModalSpec) => typeof StencilElement;
