// Image/layout export, clipboard and file IO. Holds no state of its own: it reads the
// app's editor state and routes every mutation back through the app's shared methods.
import type { DrawingApp } from '../drawingApp.js';
import type { LayoutPayload } from '../layoutInstall.js';

/** 'current' = tint + lines; 'original' = untouched crop; 'tint' = filter only; 'split' = the clean compare composite. */
export type ExportVariant = 'current' | 'original' | 'tint' | 'split';

export declare class ExportService {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** Client-side download of `blob` as `filename` via a transient <a>. */
  downloadBlob(blob: Blob, filename: string): void;
  /** One export variant on a full-resolution offscreen canvas. */
  renderExportCanvas(variant?: ExportVariant): HTMLCanvasElement;
  saveImage(variant?: ExportVariant): void;
  shareImage(): void;
  downloadJSON(): void;
  uploadJSON(e: Event): void;
  copyImageToClipboard(variant?: ExportVariant): Promise<void>;
  copyLayoutToClipboard(): Promise<void>;
  saveProjectFile(opts?: { includeTheme?: boolean }): Promise<void>;
  openProjectFile(input: File | string, opts?: { from?: unknown }): Promise<void>;
  pickAndOpenProjectFile(): Promise<void>;
  deleteProjectFile(): Promise<void>;
  applyPastedLayout(data: LayoutPayload, from?: { x: number; y: number } | null): Promise<void>;
  /** Install with no prompt and no toast; true when applied. */
  installLayout(data: LayoutPayload, opts?: { mode?: string; history?: boolean }): boolean;
}
