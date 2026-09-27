/** Paste and drop on the chat panel: media becomes attachments; the composer row is the target. */
export declare function wirePanelDrop(deps: {
  host: HTMLElement;
  dropRow: HTMLElement;
  attachFiles: (files: File[]) => Promise<void>;
}): void;
