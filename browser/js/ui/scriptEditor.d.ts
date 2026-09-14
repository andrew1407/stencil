/** The element ids of one editor's five actions and its hidden file input. */
export interface ScriptEditorIds {
  run: string;
  copy: string;
  download: string;
  clear: string;
  uploadBtn: string;
  upload: string;
}

export interface ScriptEditorOptions {
  editor: HTMLTextAreaElement;
  pre: Element;
  strip: Element;
  ids: ScriptEditorIds;
  app: { export: { downloadBlob: (blob: Blob, name: string) => void } };
  /** The surface's own run: it does the running, this reports the verdict around it. */
  onRun: (text: string) => Promise<void>;
  onUpload: (file: File) => void | Promise<void>;
  /** True while the surface is mid-run, so Run and Ctrl+Enter do not re-enter. */
  busy?: () => boolean;
}

/**
 * Wires one .stc editor onto the shared buffer: gating, the paint, Tab-indent, Ctrl+Enter
 * and the five actions. `schedule` republishes the textarea and repaints; `dispose` drops
 * the editor off the buffer.
 */
export declare const wireScriptEditor: (
  options: ScriptEditorOptions,
) => { schedule: () => void; dispose: () => void };
