// Shapes for popup/editorHandle.js — held apart from editorSection.js so callers need not
// import the whole editor-mode panel. Populated via Object.assign once editorSection.js runs.
import type { EditorModeApi } from './editorMode.js';

export declare const editorMode: EditorModeApi;
