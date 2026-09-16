import type { DrawingApp } from '../core/drawingApp.js';
import type { StencilElement } from './base.js';

/** What a meta field hands the shell: its body markup and a port onto its value. */
export interface MetaFieldPort<V> {
  set: (value: V) => void;
  get: () => V;
  focus: () => void;
  /** Drops what was typed but never committed; the shell calls it on close. */
  reset?: () => void;
}
export interface MetaField<V> {
  markup: (name: string) => string;
  /** One extra footer button, seated before Cancel. */
  footer?: (name: string) => string;
  wire: (name: string, commit: () => void) => MetaFieldPort<V>;
}

export interface ProjectMetaModalSpec<V = unknown> {
  name: string;
  title: string;
  glyph: string;
  hint: string;
  noun: string;
  addLabel: string;
  field: MetaField<V>;
  load: (meta: Record<string, unknown> | null) => V;
  save: (app: DrawingApp, id: string | number, value: V) => unknown;
}

/** The modal shell shared by descriptionModal.js and keywordsModal.js. */
export declare const createProjectMetaModal: <V>(spec: ProjectMetaModalSpec<V>) => typeof StencilElement;

/** A text area filling the body; `commitsOn` picks the Enter that saves. */
export declare const metaTextField: (opts: {
  placeholder: string;
  rows: number;
  commitsOn: (e: KeyboardEvent) => boolean;
}) => MetaField<string>;
