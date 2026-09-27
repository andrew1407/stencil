// DrawingApp's own editing behaviour that needs no DOM: the hit-test defaults, selection and
// drag starts, the undo history, and the small session answers. Installed onto the app's
// prototype as they are (delegates.js installMethods); `this` is the app.

/** The methods DrawingApp takes on; each one's shape is declared on DrawingApp (drawingApp.d.ts). */
export declare class EditingMethods {
  openInDesktopAvailable(): boolean;
  openInTelegramAvailable(): boolean;
  openInAvailable(): boolean;
  compareReadOnly(): boolean;
  compareShowsPoint(x: number, y: number): boolean;
  importExternalImage(payload: unknown, opts?: { mode?: string }): unknown;
  loadJSONFromFile(file: Blob, opts?: { from?: unknown }): void;
  findLineAt(x: number, y: number, threshold?: number): number;
  deselectLine(redraw?: boolean): void;
  findNearestPoint(x: number, y: number, threshold?: number): unknown;
  beginPullOutDrag(x: number, y: number): boolean;
  unchainSelectedLine(): void;
  findNearestPointWithIdx(x: number, y: number, threshold?: number): unknown;
  findNearestSegmentWithIdx(x: number, y: number, threshold?: number): unknown;
  saveHistory(): void;
  undo(): void;
  redo(): void;
  showSaveStatus(msg: string, color: string): void;
  hasEditingSession(): boolean;
  closeProject(id: string | null, opts?: { fully?: boolean }): this;
}
