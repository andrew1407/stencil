import type { ExportHost } from './project/projectFilePicker.js';

/** Copy one export variant of the image. Resolves on a successful write, rejects on failure. */
export declare function copyImageToClipboard(svc: ExportHost, variant?: string): Promise<void>;

/** Copy the FULL layout (lines plus every applied edit) as JSON text. */
export declare function copyLayoutToClipboard(svc: ExportHost): Promise<void>;
