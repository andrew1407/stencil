/** What these need from ExportService: the app, and its download-blob fallback. */
export interface ExportHost {
  app: Record<string, unknown>;
  downloadBlob(blob: unknown, filename: string): void;
}

/** Save the whole project as a .stencil file, linking the handle for live sync. */
export declare function saveProjectFile(svc: ExportHost, opts?: { includeTheme?: boolean }): Promise<void>;

/** Open a .stencil project from a File or raw JSON text. */
export declare function openProjectFile(svc: ExportHost, input: unknown, opts?: { from?: unknown }): Promise<void>;

/** Prompt for a .stencil file, then open it. */
export declare function pickAndOpenProjectFile(svc: ExportHost): Promise<void>;

/** Delete the linked .stencil file from disk after a confirm, then unlink. */
export declare function deleteProjectFile(svc: ExportHost): Promise<void>;
