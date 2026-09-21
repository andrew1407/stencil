/** The row's open gesture (js/core/openGesture.js), as the rename editor sees it. */
export interface ProjectRowGesture {
  cancel(): void;
  dragStart(): void;
}

/** What a saved row hands back to the modal: its gesture, and the rename it also offers. */
export interface RowActions {
  gesture: ProjectRowGesture;
  beginRename(): void;
}

/** Attach the ⋯ / right-click menu, the row prompts and every open gesture to a SAVED row. */
export function attachRowActions(deps: Record<string, unknown>): RowActions;

/** Attach the incognito row's one action: publish this session to a connected server. */
export function attachIncognitoActions(deps: { row: HTMLElement; app: unknown; render: () => void }): void;
