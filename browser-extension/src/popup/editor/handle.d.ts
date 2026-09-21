// Shapes for popup/handle.js — held apart from section.js so callers need not
// import the whole editor-mode panel. Populated via Object.assign once section.js runs.
import type { EditorModeApi } from './mode.js';

export declare const editorMode: EditorModeApi;
